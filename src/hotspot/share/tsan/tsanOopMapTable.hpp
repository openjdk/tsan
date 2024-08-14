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

#ifndef SHARE_TSAN_TSANOOPMAPTABLE_HPP
#define SHARE_TSAN_TSANOOPMAPTABLE_HPP

#include "gc/shared/collectedHeap.hpp"
#include "memory/allocation.hpp"
#include "oops/oop.inline.hpp"
#include "oops/weakHandle.hpp"
#include "tsan/tsanOopMap.hpp"
#include "utilities/resizeableResourceHash.hpp"

class TsanOopMapTableKey;

namespace TsanOopMapImpl {

  struct PendingMove {
    char *source_begin() const { return source_address; }
    char *source_end() const { return source_address + n_bytes; }
    char *target_begin() const { return target_address; }
    char *target_end() const { return target_address + n_bytes; }
    char *source_address;
    char *target_address;
    size_t n_bytes;  // number of bytes being moved
  };

  struct MovedEntry {
    TsanOopMapTableKey* k;
    size_t v;
    TsanOopMapTableKey* key() const { return k; }
    size_t value() const { return v; }
  };

}  // namespace TsanOopMapImpl

// For tracking the lifecycle (alloc/move/free) of interesting oops
// that tsan needs to know.
class TsanOopMapTableKey : public CHeapObj<mtInternal> {
 private:
  WeakHandle _wh;

  // Pointer to the oop tracked by the WeakHandle.
  // After an object is freed, the WeakHandle points to null oop. We
  // need to cache the original oop for notifying Tsan after the object
  // is freed.
  oop _obj;

 public:
  TsanOopMapTableKey(oop obj);
  TsanOopMapTableKey(const TsanOopMapTableKey& src);
  TsanOopMapTableKey& operator=(const TsanOopMapTableKey&) = delete;

  void release_weak_handle() const;
  oop object_no_keepalive() const;

  oop obj() const { return _obj; };
  void update_obj();

  // Compute the hash for the entry using the enclosed oop address.
  // Note that this would return a different hash value when an oop
  // enclosed by the entry is moved by GC. When that happens, we need
  // to remove the old entry from the tsanOopMap and insert a new
  // entry using re-computed hash. That's to prevent the same `oop`
  // being added to the tsanOopMap and notifing tsan (when `oop` is
  // moved) more than once.
  //
  // We cannot use the `oop` identity hash here, as we need to compute
  // the hash when trying to add a new `oop` to the tsanOopMap. One of
  // the case is during InterpreterMacroAssembler::lock_object, which
  // may cause a new identity hash being computed for an `oop` in some
  // cases. That could be a hidden issue with `oop` identity hash.
  static unsigned get_hash(const TsanOopMapTableKey& entry) {
    assert(entry._obj != nullptr, "sanity");
    assert(entry._obj == entry.object_no_keepalive(), "sanity");
    return primitive_hash<oopDesc*>(entry.object_no_keepalive());
  }

  static bool equals(const TsanOopMapTableKey& lhs, const TsanOopMapTableKey& rhs) {
    return lhs.object_no_keepalive() == rhs.object_no_keepalive();
  }
};

typedef
ResizeableResourceHashtable <TsanOopMapTableKey, size_t,
                             AnyObj::C_HEAP, mtInternal,
                             TsanOopMapTableKey::get_hash,
                             TsanOopMapTableKey::equals> RRHT;

// The TsanOopMapTable contains entries of TsanOopMapTableKey:oop_size pairs
// (as key:value). The oop sizes are saved in the table because we need to
// use the size information when notify TSAN about an freed object.
class TsanOopMapTable : public CHeapObj<mtInternal> {
 private:
  RRHT _table;

 public:
  TsanOopMapTable();
  ~TsanOopMapTable();

  void clear();

  unsigned size() const { return _table.table_size(); };

  bool add_entry(TsanOopMapTableKey *entry, size_t size);
  bool add_oop_with_size(oop obj, size_t size);

#ifdef ASSERT
  bool   is_empty();
  size_t find(oop obj);
#endif

  void collect_moved_objects_and_notify_freed(
           GrowableArray<TsanOopMapImpl::MovedEntry> *moved_entries,
           GrowableArray<TsanOopMapImpl::PendingMove> *moves,
           char **src_low, char **src_high,
           char **dest_low, char **dest_high,
           int  *n_downward_moves);
};

#endif // SHARE_TSAN_TSANOOPMAPTABLE_HPP
