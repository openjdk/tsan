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

extern "C" int jio_printf(const char *fmt, ...);

#if 0
#define DEBUG_PRINT(...) jio_printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

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

  class TsanOopBitMap : public CHeapBitMap {
    public:
      TsanOopBitMap() : CHeapBitMap(mtInternal) {}
      TsanOopBitMap(idx_t size_in_bits) : CHeapBitMap(size_in_bits, mtInternal) {}

      // Following functions are from JDK 11 BitMap. They no longer exist in JDK 21.
      static idx_t word_index(idx_t bit)  { return bit >> LogBitsPerWord; }
      static idx_t word_align_up(idx_t bit) {
        return align_up(bit, BitsPerWord);
      }
      static bool is_word_aligned(idx_t bit) {
        return word_align_up(bit) == bit;
      }

      // This is from JDK 11 BitMap. 
      idx_t get_next_one_offset(idx_t l_offset, idx_t r_offset) const {
        assert(l_offset <= size(), "BitMap index out of bounds");
        assert(r_offset <= size(), "BitMap index out of bounds");
        assert(l_offset <= r_offset, "l_offset > r_offset ?");

        if (l_offset == r_offset) {
          return l_offset;
        }
        idx_t   index = word_index(l_offset);
        idx_t r_index = word_index(r_offset-1) + 1;
        idx_t res_offset = l_offset;

        // check bits including and to the _left_ of offset's position
        idx_t pos = bit_in_word(res_offset);
        bm_word_t res = map(index) >> pos;
        if (res != 0) {
          // find the position of the 1-bit
          for (; !(res & 1); res_offset++) {
            res = res >> 1;
          }

#ifdef ASSERT
          // In the following assert, if r_offset is not bitamp word aligned,
          // checking that res_offset is strictly less than r_offset is too
          // strong and will trip the assert.
          //
          // Consider the case where l_offset is bit 15 and r_offset is bit 17
          // of the same map word, and where bits [15:16:17:18] == [00:00:00:01].
          // All the bits in the range [l_offset:r_offset) are 0.
          // The loop that calculates res_offset, above, would yield the offset
          // of bit 18 because it's in the same map word as l_offset and there
          // is a set bit in that map word above l_offset (i.e. res != NoBits).
          //
          // In this case, however, we can assert is that res_offset is strictly
          // less than size() since we know that there is at least one set bit
          // at an offset above, but in the same map word as, r_offset.
          // Otherwise, if r_offset is word aligned then it will not be in the
          // same map word as l_offset (unless it equals l_offset). So either
          // there won't be a set bit between l_offset and the end of it's map
          // word (i.e. res == NoBits), or res_offset will be less than r_offset.

          idx_t limit = is_word_aligned(r_offset) ? r_offset : size();
          assert(res_offset >= l_offset && res_offset < limit, "just checking");
#endif // ASSERT
          return MIN2(res_offset, r_offset);
        }
        // skip over all word length 0-bit runs
        for (index++; index < r_index; index++) {
          res = map(index);
          if (res != 0) {
            // found a 1, return the offset
            for (res_offset = bit_index(index); !(res & 1); res_offset++) {
              res = res >> 1;
            }
            assert(res & 1, "tautology; see loop condition");
            assert(res_offset >= l_offset, "just checking");
            return MIN2(res_offset, r_offset);
          }
        }
        return r_offset;
      } 
  };

  // Maintains the occupancy state of the given heap memory area.
  class OccupancyMap: public StackObj {
    // Internally it is a BitMap. A bit is set if the corresponding HeapWord
    // is currently occupied, cleared otherwise (HeapWord is Java object
    // allocation unit).
    char *mem_begin_;
    char *mem_end_;
    TsanOopBitMap bitmap_;
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
          bitmap_((mem_end - mem_begin) / HeapWordSize) {}
    bool is_range_vacant(char *from, char *to) const {
      assert(from < to, "bad range");
      assert(from >= mem_begin_ && from < mem_end_,
             "start address outside range");
      assert(to > mem_begin_ && to <= mem_end_, "end address outside range");
      BitMap::idx_t idx_to = to_idx(to);
      return bitmap_.get_next_one_offset(to_idx(from), idx_to) == idx_to;
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
    DEBUG_PRINT("%s:%d: %d objects occupying %d words between %p and %p\n",
                __FUNCTION__, __LINE__, moves.length(),
                occupied_memory.bit_count(),
                MIN2(source_low, target_low),
                MAX2(source_high, target_high));
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
      DEBUG_PRINT("%s:%d: %d moved, %d remaining\n", __FUNCTION__, __LINE__,
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

void TsanOopMap::gc_notification(size_t num_dead_entries) {
  // FIXME
  TSAN_RUNTIME_ONLY(
    //MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    //trigger_concurrent_work(); 
  );
}

// FIXME: Called by GC threads.
void TsanOopMap::notify_tsan_for_freed_and_moved_objects() {
  if (_oop_map == nullptr) {
    return;
  }

  bool disjoint_regions;
  int n_downward_moves = 0;
  char *min_low, *max_high;
  char *source_low = reinterpret_cast<char *>(UINTPTR_MAX);
  char *source_high = NULL;
  char *target_low = reinterpret_cast<char *>(UINTPTR_MAX);
  char *target_high = NULL;

  ResourceMark rm;
  GrowableArray<TsanOopMapImpl::PendingMove> moves(MAX2((int)(_oop_map->size()), 100000));

  MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
  {
    _oop_map->collect_moved_objects_and_notify_freed(
                                 &moves, &source_low, &source_high,
                                 &target_low, &target_high,
                                 &n_downward_moves);
  }

  // No lock is needed after this point. FIXME
  if (moves.length() != 0) {
    // Notify Tsan about moved objects.
    disjoint_regions = (source_low >= target_high || source_high <= target_low);
    min_low = MIN2(source_low, target_low);
    max_high = MAX2(source_high, target_high);

    moves.sort((2 * n_downward_moves > moves.length()) ?
                  TsanOopMapImpl::lessThan : TsanOopMapImpl::moreThan);
    if (disjoint_regions) {
      for (int i = 0; i < moves.length(); ++i) {
        const TsanOopMapImpl::PendingMove &m = moves.at(i);
        log_trace(tsan)("__tsan_java_move for [" PTR_FORMAT ", " PTR_FORMAT  "] -> [" PTR_FORMAT ", " PTR_FORMAT "]\n",
                       (uintx)m.source_begin(), (uintx)m.source_end(),
                       (uintx)m.target_begin(), (uintx)m.target_end());
        __tsan_java_move(m.source_begin(), m.target_begin(), m.n_bytes);
      }
    } else {
      handle_overlapping_moves(moves, min_low, max_high);
    }
  }
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
  jlong oop_size = 0;
  {
    MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    oop_size = _oop_map->find(addr); 
  }
  return oop_size != 0;
}
#endif
