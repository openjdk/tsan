/*
 * Copyright (c) 2024, Google and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_PRIMS_TSAN_OOPMAPTABLE_HPP
#define SHARE_VM_PRIMS_TSAN_OOPMAPTABLE_HPP

#include "gc/shared/collectedHeap.hpp"
#include "memory/allocation.hpp"
#include "oops/oop.inline.hpp"
#include "oops/weakHandle.hpp"
#include "tsan/tsanOopMap.hpp"
#include "utilities/resizeableResourceHash.hpp"

// For tracking the lifecycle (alloc/move/free) of interesting oops
// that tsan needs to know.
class TsanOopMapTableKey : public CHeapObj<mtInternal> {
 private:
  WeakHandle _wh;

  // Address of the oop pointed to by the WeakHandle. Note tis is not
  // updated by GC.
  oopDesc *_obj;

  // We cache the oop's size, since we cannot reliably determine it after
  // the oop is freed. Size is measured in number of HeapWords.
  // FIXME: needed? Remove!
  uintx _size;

 public:
  TsanOopMapTableKey(oop obj);
  TsanOopMapTableKey(oop obj, int size);
  //TsanOopMapTableKey(const TsanOopMapTableKey& src);
  TsanOopMapTableKey& operator=(const TsanOopMapTableKey&) = delete;

  void release_weak_handle() const;
  oop object_no_keepalive() const;

  oop obj() const { return _obj; };
  void update_obj(); 

  static unsigned get_hash(const TsanOopMapTableKey& entry) {
    // FIXME
    assert(entry._obj != nullptr, "sanity");
    return (unsigned int)entry._obj->identity_hash();
  }

  static bool equals(const TsanOopMapTableKey& lhs, const TsanOopMapTableKey& rhs) {
    // FIXME: check size?
    oop lhs_obj = lhs._obj != nullptr ? lhs._obj : lhs.object_no_keepalive();
    oop rhs_obj = rhs._obj != nullptr ? rhs._obj : rhs.object_no_keepalive();
    return lhs_obj == rhs_obj; 
  } 
};

typedef
ResizeableResourceHashtable <TsanOopMapTableKey, jlong,
                             AnyObj::C_HEAP, mtInternal,
                             TsanOopMapTableKey::get_hash,
                             TsanOopMapTableKey::equals> RRHT;

class TsanOopMapTable : public CHeapObj<mtInternal> {
 private:
  RRHT _table;
  bool _needs_cleaning;

 public:
  TsanOopMapTable();
  ~TsanOopMapTable();

  unsigned size() const { return _table.table_size(); };

  bool is_empty(); 
  void set_needs_cleaning(bool need_cleaning) { _needs_cleaning = need_cleaning; }

  bool add_oop_with_size(oop obj, int size);

  jlong find(oop obj);

  void do_concurrent_work(GrowableArray<TsanOopMapImpl::PendingMove> *moves,
                          char **src_low, char **src_high,
                          char **dest_low, char **dest_high,
                          int  *n_downward_moves);
};

#endif // SHARE_VM_PRIMS_TSAN_OOPMAPTABLE_HPP
