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
#include "runtime/mutexLocker.hpp"
#include "runtime/safepointVerifiers.hpp"
#include "tsan/tsanExternalDecls.hpp"
#include "tsan/tsanOopMap.hpp"
#include "tsan/tsanOopMapTable.hpp"
#include "utilities/bitMap.inline.hpp"

namespace TsanOopMapImpl {
  // Two little callbacks used by sort.
  int lessThan(PendingMove *l, PendingMove *r) {
    char *left = l->target_begin();
    char *right = r->target_begin();
    return (left < right) ? -1 : (left == right ? 0 : 1);
  }

  int moreThan(PendingMove *l, PendingMove *r) {
    return lessThan(r, l);
  }

  // Maintains the occupancy state of the given heap memory area.
  class OccupancyMap: public StackObj {
    // Internally it is a BitMap. A bit is set if the corresponding HeapWord
    // is currently occupied, cleared otherwise (HeapWord is Java object
    // allocation unit).
    char *mem_begin_;
    char *mem_end_;
    CHeapBitMap bitmap_;
    BitMap::idx_t to_idx(char *mem) const {
      return (mem - mem_begin_) / HeapWordSize;
    }
  public:
    // NOTE: The constructor creates a bitmap on the resource area.
    // The bitmap can be quite large (it is 16MB per every 1GB of heap,
    // so it is worth releasing it as soon as possible by creating a
    // ResourceMark.
    OccupancyMap(char *mem_begin, char *mem_end)
        : mem_begin_(mem_begin), mem_end_(mem_end),
          bitmap_((mem_end - mem_begin) / HeapWordSize, mtInternal) {}
    bool is_range_vacant(char *from, char *to) const {
      assert(from < to, "bad range");
      assert(from >= mem_begin_ && from < mem_end_,
             "start address outside range");
      assert(to > mem_begin_ && to <= mem_end_, "end address outside range");
      BitMap::idx_t idx_to = to_idx(to);
      return bitmap_.find_first_set_bit(to_idx(from), idx_to) == idx_to;
    }
    void range_occupy(char *from, char *to) {
      assert(from < to, "range_occupy: bad range");
      assert(from >= mem_begin_ && from < mem_end_,
             "start address outside range");
      assert(to > mem_begin_ && to <= mem_end_, "end address outside range");
      bitmap_.set_range(to_idx(from), to_idx(to));
    }
    void range_vacate(char *from, char *to) {
      assert(from < to, "bad range");
      assert(from >= mem_begin_ && from < mem_end_,
             "start address outside range");
      assert(to > mem_begin_ && to <= mem_end_, "end address outside range");
      bitmap_.clear_range(to_idx(from), to_idx(to));
    }
    int bit_count() const {
      return bitmap_.size();
    }
  };

  static void handle_overlapping_moves(GrowableArray<PendingMove>& moves,
                                                char* min_low,
                                                char* max_high) {
    // Populate occupied memory. The bitmap allocated by the OccupancyMap can
    // be fairly large, scope this code and insert a ResourceMark
    ResourceMark rm;
    OccupancyMap occupied_memory(min_low, max_high);
    log_debug(tsan)("%s:%d: %d objects occupying %d words between %p and %p\n",
                    __FUNCTION__, __LINE__, moves.length(),
                    occupied_memory.bit_count(),
                    min_low, max_high);
    for (int i = 0; i < moves.length(); ++i) {
      PendingMove &m = moves.at(i);
      occupied_memory.range_occupy(m.source_begin(), m.source_end());
    }

    // Keep traversing moves list until everything is moved
    int passes = 0;
    for (int remaining_moves = moves.length(); remaining_moves > 0; ) {
      ++passes;
      int moves_this_cycle = 0;
      for (int i = 0; i < moves.length(); ++i) {
        if (moves.at(i).n_bytes == 0) {
           // Already moved this one.
           continue;
        }

        // Check if this move is currently possible.
        // For this, everything in the target region that is not in the source
        // region has to be vacant.
        bool can_move;
        PendingMove &m = moves.at(i);
        if (m.target_begin() < m.source_begin()) {
          // '+++++++' is region being moved, lower addresses are to the left:
          // Moving downwards:
          //         ++++++++         SOURCE
          //    ++++++++              TARGET
          // or
          //              ++++++++    SOURCE
          //    ++++++++              TARGET
          can_move = occupied_memory.is_range_vacant(
              m.target_begin(), MIN2(m.target_end(), m.source_begin()));
        } else {
          // Moving upwards:
          //    ++++++++              SOURCE
          //         ++++++++         TARGET
          // or
          //    ++++++++              SOURCE
          //              ++++++++    TARGET
          can_move = occupied_memory.is_range_vacant(
              MAX2(m.source_end(), m.target_begin()), m.target_end());
        }
        if (can_move) {
          // Notify TSan, update occupied region.
          log_trace(tsan)("__tsan_java_move for [" PTR_FORMAT ", " PTR_FORMAT  "] -> [" PTR_FORMAT ", " PTR_FORMAT "]\n",
                          (uintx)m.source_begin(), (uintx)m.source_end(),
                          (uintx)m.target_begin(), (uintx)m.target_end());
          __tsan_java_move(m.source_begin(), m.target_begin(), m.n_bytes);
          occupied_memory.range_vacate(m.source_begin(), m.source_end());
          occupied_memory.range_occupy(m.target_begin(), m.target_end());
          // Indicate that this move has been done and remember that we
          // made some progress.
          m.n_bytes = 0;
          ++moves_this_cycle;
        }
      }
      // We have to make some progress, otherwise bail out:
      guarantee(moves_this_cycle, "Impossible to reconcile GC");

      guarantee(remaining_moves >= moves_this_cycle,
                "Excessive number of moves");
      remaining_moves -= moves_this_cycle;
      log_debug(tsan)("%s:%d: %d moved, %d remaining\n", __FUNCTION__, __LINE__,
                       moves_this_cycle, remaining_moves);
    }
    log_debug(gc)("Tsan: Move %d passes", passes);
  }
}  // namespace TsanOopMapImpl

static OopStorage* _weak_oop_storage;
static TsanOopMapTable* _oop_map;

// This is called with TSAN_ONLY, as we want to always create the weak
// OopStorage so the number matches with the 'weak_count' in oopStorageSet.hpp.
void TsanOopMap::initialize_map() {
  // No need to do register_num_dead_callback for concurrent work as we do
  // TsanOopMapTable cleanup, i.e. removing entries for freed objects during
  // GC by calling TsanOopMap::notify_tsan_for_freed_and_moved_objects() from
  // WeakProcessor.
  _weak_oop_storage = OopStorageSet::create_weak("Tsan weak OopStorage", mtInternal);
  assert(_weak_oop_storage != nullptr, "sanity");

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

// Called during GC by WeakProcessor.
void TsanOopMap::notify_tsan_for_freed_and_moved_objects() {
  assert(_oop_map != nullptr, "must be");
  assert(SafepointSynchronize::is_at_safepoint(), "must be");

  bool disjoint_regions;
  int n_downward_moves = 0;
  char *min_low, *max_high;
  char *source_low = reinterpret_cast<char *>(UINTPTR_MAX);
  char *source_high = NULL;
  char *target_low = reinterpret_cast<char *>(UINTPTR_MAX);
  char *target_high = NULL;

  int len = MAX2((int)(_oop_map->size()), 100000);
  ResourceMark rm;
  GrowableArray<TsanOopMapImpl::PendingMove> moves(len);
  GrowableArray<TsanOopMapTableKey*> moved_entries(len);

  {
    MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    _oop_map->collect_moved_objects_and_notify_freed(
                                 &moved_entries,
                                 &moves, &source_low, &source_high,
                                 &target_low, &target_high,
                                 &n_downward_moves);

    // Add back the entries with moved oops. New hashes for the entries
    // are computed using the new oop address.
    for (int i = 0; i < moved_entries.length(); i++) {
      TsanOopMapTableKey* entry = moved_entries.at(i);
      _oop_map->add_entry(entry, entry->obj()->size());
    }
  }

  // No lock is needed after this point.
  if (moves.length() != 0) {
    // Notify Tsan about moved objects.
    disjoint_regions = (source_low >= target_high || source_high <= target_low);
    min_low = MIN2(source_low, target_low);
    max_high = MAX2(source_high, target_high);

    if (disjoint_regions) {
      for (int i = 0; i < moves.length(); ++i) {
        const TsanOopMapImpl::PendingMove &m = moves.at(i);
        log_trace(tsan)("__tsan_java_move for [" PTR_FORMAT ", " PTR_FORMAT  "] -> [" PTR_FORMAT ", " PTR_FORMAT "]\n",
                        (uintx)m.source_begin(), (uintx)m.source_end(),
                        (uintx)m.target_begin(), (uintx)m.target_end());
        __tsan_java_move(m.source_begin(), m.target_begin(), m.n_bytes);
      }
    } else {
      // Source and target ranges overlap, the moves need to be ordered to prevent
      // overwriting. Overall, this can take N^2 steps if only one object can be
      // moved during the array traversal.
      moves.sort((2 * n_downward_moves > moves.length()) ?
                 TsanOopMapImpl::lessThan : TsanOopMapImpl::moreThan);
      handle_overlapping_moves(moves, min_low, max_high);
    }
  }
}

// Safe to deal with raw oop; for example this is called in a LEAF function
// There is no safepoint in this code: 1) special mutex is used, and
// 2) there is no VM state transition
// We cannot use ordinary VM mutex, as that requires a state transition.
void TsanOopMap::add_oop_with_size(oopDesc *addr, size_t size) {
  DEBUG_ONLY(NoSafepointVerifier nsv;)
  assert(_oop_map != NULL, "TsanOopMapTable not initialized");
  guarantee(addr != NULL, "null oop");
  bool added = false;
  {
    MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    added = _oop_map->add_oop_with_size(addr, size);
  }
  if (added) {
    log_trace(tsan)("__tsan_java_alloc for: " PTR_FORMAT ", " PTR_FORMAT "\n",
                    (uintx)addr, (uintx)addr + size * HeapWordSize);
    __tsan_java_alloc(addr, size * HeapWordSize);
  }
}

void TsanOopMap::add_oop(oopDesc *addr) {
  // We need object size when notify tsan about a freed object.
  // We cannot call size() for an object after it's freed, so we
  // need to save the size information in the table.
  add_oop_with_size(addr, addr->size());
}

#ifdef ASSERT
bool TsanOopMap::exists(oopDesc *addr) {
  DEBUG_ONLY(NoSafepointVerifier nsv;)
  assert(_oop_map != NULL, "TsanOopMap not initialized");
  guarantee(addr != NULL, "null oop");
  size_t oop_size = 0;
  {
    MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    oop_size = _oop_map->find(addr);
  }
  return oop_size != 0;
}
#endif
