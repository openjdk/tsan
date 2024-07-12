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
  // FIXME: set size
}

TsanOopMapTableKey::TsanOopMapTableKey(oop obj, int size) {
  _wh = WeakHandle(TsanOopMap::oop_storage(), obj);
  _obj = obj;
  _size = size;
}

#if 0
TsanOopMapTableKey::TsanOopMapTableKey(const TsanOopMapTableKey& src) {
  // FIXME
  _wh.release(TsanOopMap::oop_storage());
  _wh = WeakHandle();
  _obj = nullptr;
  _size = 0;
}
#endif

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

TsanOopMapTable::~TsanOopMapTable() {
  // FIXME
}

bool TsanOopMapTable::is_empty() {
  //assert(SafepointSynchronize::is_at_safepoint(), "sanity");
  // FIXME: check if locked
  return _table.number_of_entries() == 0; 
}

bool TsanOopMapTable::add_oop_with_size(oop obj, int size) {
  TsanOopMapTableKey new_entry(obj);
  bool added;
  if (obj->fast_no_hash_check()) {
    added = _table.put_when_absent(new_entry, size); 
  } else {
    jlong* v = _table.put_if_absent(new_entry, size, &added);
    *v = size;
  }
  return added;
}

jlong TsanOopMapTable::find(oop obj) {
  if (is_empty()) {
    return 0;
  }

  if (obj->fast_no_hash_check()) {
    return 0;
  }

  TsanOopMapTableKey item(obj);
  jlong* size = _table.get(item);
  return size == nullptr ? 0 : *size;
}

void TsanOopMapTable::do_concurrent_work(GrowableArray<TsanOopMapImpl::PendingMove> *moves,
                                         char **src_low, char **src_high,
                                         char **dest_low, char **dest_high,
                                         int *n_downward_moves) {
  struct IsDead {
    GrowableArray<TsanOopMapImpl::PendingMove> *_moves;
    char **_src_low;
    char **_src_high;
    char **_dest_low;
    char **_dest_high;
    int  *_n_downward_moves;
    IsDead(GrowableArray<TsanOopMapImpl::PendingMove> *moves,
           char **src_low, char **src_high,
           char **dest_low, char **dest_high,
           int  *n_downward_moves) : _moves(moves), _src_low(src_low), _src_high(src_high),
                                     _dest_low(dest_low), _dest_high(dest_high),
                                     _n_downward_moves(n_downward_moves) {}
    bool do_entry(TsanOopMapTableKey& entry, uintx size) {
      oop wh_obj = entry.object_no_keepalive();
      if (wh_obj == nullptr) {
        // FIXME
        tty->print("##### __tsan_java_free " PTR_FORMAT "\n", (long unsigned int)entry.obj());
        __tsan_java_free((char *)entry.obj(), size * HeapWordSize);
        entry.release_weak_handle();
        //auto clean = [] (const TsanOopMapTableKey& entry) {
        //  entry.release_weak_handle();
        //};
        //_table.remove(entry, clean);

        return true;
      } else if (wh_obj != entry.obj()) {
        //tty->print("##### __tsan_java_move " PTR_FORMAT " -> " PTR_FORMAT "\n", (long unsigned int)entry.obj(), (long unsigned int)wh_obj);

        TsanOopMapImpl::PendingMove move =
          {(char *)entry.obj(), (char *)wh_obj, size * HeapWordSize};
        _moves->append(move);
        *_src_low = MIN2(*_src_low, move.source_begin());
        *_src_high = MAX2(*_src_high, move.source_end());
        *_dest_low = MIN2(*_dest_low, move.target_begin());
        *_dest_high = MAX2(*_dest_high, move.target_end());
        if (*_dest_low < *_src_low) {
          ++(*_n_downward_moves);
        }

        //__tsan_java_move(entry.obj(), wh_obj, size * HeapWordSize);
        entry.update_obj(); 
      }
      return false;
    } 
  } is_dead(moves, src_low, src_high, dest_low, dest_high, n_downward_moves);
  _table.unlink(&is_dead);
}
