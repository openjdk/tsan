/*
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2019, Google and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "gc/shared/gcTraceTime.inline.hpp"
#include "gc/shared/gcId.hpp"
#include "gc/shared/oopStorageSet.hpp"
#include "gc/shared/referenceProcessor.hpp"
#include "memory/resourceArea.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/atomic.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/safepointVerifiers.hpp"
#include "tsan/tsanExternalDecls.hpp"
#include "tsan/tsanOopMap.hpp"
#include "tsan/tsanOopMapTable.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/resizeableResourceHash.hpp"

volatile bool TsanOopMap::_has_work = false; 
static OopStorage* _weak_oop_storage;
static TsanOopMapTable* _oop_map;

DEBUG_ONLY(static bool notified_needs_cleaning = false;)
static bool _needs_cleaning = false;

// This is called with TSAN_ONLY, as we want to always create the weak
// OopStorage.
void TsanOopMap::initialize_map() {
  _weak_oop_storage = OopStorageSet::create_weak("Tsan weak OopStorage", mtInternal);
  assert(_weak_oop_storage != NULL, "sanity");
  _weak_oop_storage->register_num_dead_callback(&TsanOopMap::gc_notification);

  TSAN_RUNTIME_ONLY(
    _oop_map = new TsanOopMapTable();
  );
}

void TsanOopMap::destroy() {
  delete _oop_map;
}

OopStorage* TsanOopMap::oop_storage() {
  assert(_weak_oop_storage != NULL, "sanity");
  return _weak_oop_storage;
}

void TsanOopMap::set_needs_cleaning() {
  assert(SafepointSynchronize::is_at_safepoint(), "called in gc pause");
  assert(Thread::current()->is_VM_thread(), "should be the VM thread");
  DEBUG_ONLY(notified_needs_cleaning = true;)
  // FIXME: lock?
  _oop_map->set_needs_cleaning(!_oop_map->is_empty());
} 

void TsanOopMap::gc_notification(size_t num_dead_entries) {
  TSAN_RUNTIME_ONLY(
    assert(notified_needs_cleaning, "missing GC notification");
    DEBUG_ONLY(notified_needs_cleaning = false;)

    MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    trigger_concurrent_work(); 
    _oop_map->set_needs_cleaning(false);
  );
}

void TsanOopMap::trigger_concurrent_work() {
  MutexLocker ml(Service_lock, Mutex::_no_safepoint_check_flag);
  Atomic::store(&_has_work, true);
  Service_lock->notify_all();
}

bool TsanOopMap::has_work() {
  return Atomic::load_acquire(&_has_work);
}

void TsanOopMap::do_concurrent_work(JavaThread*) {
  assert_not_at_safepoint();

  {
    MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    _oop_map->do_concurrent_work();
  }

  Atomic::release_store(&_has_work, false);
}

// Safe to deal with raw oop; for example this is called in a LEAF function
// There is no safepoint in this code: 1) special mutex is used, and
// 2) there is no VM state transition
// We cannot use ordinary VM mutex, as that requires a state transition.
void TsanOopMap::add_oop_with_size(oopDesc *addr, int size) {
  DEBUG_ONLY(NoSafepointVerifier nsv;)
  assert(_oop_map != NULL, "TsanOopMapTable not initialized");
  guarantee(addr != NULL, "null oop");
  bool added = false;
  {
    MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    // FIXME!!! N.B. addr->size() may not be available yet!
    added = _oop_map->add_oop_with_size(addr, size);  
  }
  if (added) {
    tty->print("##### __tsan_java_alloc: " PTR_FORMAT "\n", (long unsigned int)addr);
    __tsan_java_alloc(addr, size * HeapWordSize);
  }
}

void TsanOopMap::add_oop(oopDesc *addr) {
  // N.B. oop's size field must be init'ed; else addr->size() crashes.
  add_oop_with_size(addr, addr->size());
}

#ifdef ASSERT
bool TsanOopMap::exists(oopDesc *addr) {
  DEBUG_ONLY(NoSafepointVerifier nsv;)
  assert(_oop_map != NULL, "TsanOopMap not initialized");
  guarantee(addr != NULL, "null oop");
  jlong oop_size = 0;
  {
    MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    oop_size = _oop_map->find(addr); 
  }
  return oop_size != 0;
}
#endif
