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

#include "precompiled.hpp"
#include "tsan/tsanExternalDecls.hpp"
#include "tsan/tsanOopMap.hpp"
#include "tsan/tsanOopMapTable.hpp"

TsanOopMapTableKey::TsanOopMapTableKey(oop obj) {
  _wh = WeakHandle(TsanOopMap::oop_storage(), obj);
  _obj = obj;
}

TsanOopMapTableKey::TsanOopMapTableKey(const TsanOopMapTableKey& src) {
  _wh = src._wh;
  _obj = src._obj;
}

void TsanOopMapTableKey::release_weak_handle() const {
  _wh.release(TsanOopMap::oop_storage());
}

oop TsanOopMapTableKey::object_no_keepalive() const {
  return _wh.peek();
}

void TsanOopMapTableKey::update_obj() {
  oop obj = _wh.peek();
  if (obj != nullptr && obj != _obj) {
    _obj = obj;
  }
}

TsanOopMapTable::TsanOopMapTable() : _table(512, 0x3fffffff) {}

void TsanOopMapTable::clear() {
  struct RemoveAll {
    bool do_entry(const TsanOopMapTableKey & entry, size_t size) {
      entry.release_weak_handle();
      return true;
    }
  } remove_all;

  _table.unlink(&remove_all);
  assert(_table.number_of_entries() == 0, "invariant");
}

TsanOopMapTable::~TsanOopMapTable() {
  clear();
}

bool TsanOopMapTable::add_entry(TsanOopMapTableKey *entry, size_t size) {
  bool added;
  size_t* v = _table.put_if_absent(*entry, size, &added);
  assert(added, "must be");
  assert(*v == size, "sanity");
  return added;
}

bool TsanOopMapTable::add_oop_with_size(oop obj, size_t size) {
  TsanOopMapTableKey new_entry(obj);
  bool added;
  if (obj->fast_no_hash_check()) {
    added = _table.put_when_absent(new_entry, size);
  } else {
    size_t* v = _table.put_if_absent(new_entry, size, &added);
    assert(*v == size, "sanity");
  }

  if (added) {
    if (_table.maybe_grow(true /* use_large_table_sizes */)) {
      log_info(tsan)("TsanOopMapTable resize to %d, %d entries",
                     _table.table_size(), _table.number_of_entries());
    }
  }
  return added;
}

#ifdef ASSERT
bool TsanOopMapTable::is_empty() {
  assert(TsanOopMap_lock->is_locked(), "sanity check");
  return _table.number_of_entries() == 0;
}

size_t TsanOopMapTable::find(oop obj) {
  if (is_empty()) {
    return 0;
  }

  if (obj->fast_no_hash_check()) {
    return 0;
  }

  TsanOopMapTableKey item(obj);
  size_t* size = _table.get(item);
  return size == nullptr ? 0 : *size;
}
#endif

// - Notify Tsan about freed objects.
// - Colllect objects moved bt GC and add a PendingMove for each moved
//   objects in a GrowableArray.
void TsanOopMapTable::collect_moved_objects_and_notify_freed(
         GrowableArray<TsanOopMapTableKey*> *moved_entries,
         GrowableArray<TsanOopMapImpl::PendingMove> *moves,
         char **src_low, char **src_high,
         char **dest_low, char **dest_high,
         int *n_downward_moves) {
  struct IsDead {
    GrowableArray<TsanOopMapTableKey*> *_moved_entries;
    GrowableArray<TsanOopMapImpl::PendingMove> *_moves;
    char **_src_low;
    char **_src_high;
    char **_dest_low;
    char **_dest_high;
    int  *_n_downward_moves;
    IsDead(GrowableArray<TsanOopMapTableKey*> *moved_entries,
           GrowableArray<TsanOopMapImpl::PendingMove> *moves,
           char **src_low, char **src_high,
           char **dest_low, char **dest_high,
           int  *n_downward_moves) : _moved_entries(moved_entries), _moves(moves),
                                     _src_low(src_low), _src_high(src_high),
                                     _dest_low(dest_low), _dest_high(dest_high),
                                     _n_downward_moves(n_downward_moves) {}
    bool do_entry(TsanOopMapTableKey& entry, size_t size) {
      oop wh_obj = entry.object_no_keepalive();
      if (wh_obj == nullptr) {
        log_trace(tsan)("__tsan_java_free for " PTR_FORMAT "\n", cast_from_oop<uintx>(entry.obj()));
        __tsan_java_free(cast_from_oop<char*>(entry.obj()), size * HeapWordSize);
        entry.release_weak_handle();
        return true;
      } else if (wh_obj != entry.obj()) {
        TsanOopMapImpl::PendingMove move =
          {cast_from_oop<char*>(entry.obj()), cast_from_oop<char*>(wh_obj), size * HeapWordSize};
        _moves->append(move);
        *_src_low = MIN2(*_src_low, move.source_begin());
        *_src_high = MAX2(*_src_high, move.source_end());
        *_dest_low = MIN2(*_dest_low, move.target_begin());
        *_dest_high = MAX2(*_dest_high, move.target_end());
        if (move.target_begin() < move.source_begin()) {
          ++(*_n_downward_moves);
        }

        entry.update_obj();

        TsanOopMapTableKey* new_entry = new TsanOopMapTableKey(entry);
        _moved_entries->append(new_entry);

        // Unlink the entry without releasing the weak_handle.
        return true;
      }
      return false;
    }
  } is_dead(moved_entries, moves, src_low, src_high, dest_low, dest_high, n_downward_moves);
  _table.unlink(&is_dead);
}
