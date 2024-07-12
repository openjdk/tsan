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
#if 0
  struct PendingMove {
    char *source_begin() const { return source_address; }
    char *source_end() const { return source_address + n_bytes; }
    char *target_begin() const { return target_address; }
    char *target_end() const { return target_address + n_bytes; }
    char *source_address;
    char *target_address;
    size_t n_bytes;  // number of bytes being moved
  };
#endif

  // Two little callbacks used by sort.
  int lessThan(PendingMove *l, PendingMove *r) {
    char *left = l->target_begin();
    char *right = r->target_begin();
    return (left < right) ? -1 : (left == right ? 0 : 1);
  }

  int moreThan(PendingMove *l, PendingMove *r) {
    return lessThan(r, l);
  }

  // FIXME
  class TsanOopBitMap : public CHeapBitMap {
    public:
      TsanOopBitMap() : CHeapBitMap(mtInternal) {}
      TsanOopBitMap(idx_t size_in_bits) : CHeapBitMap(size_in_bits, mtInternal) {}

      // From JDK 11.
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
#if 0
    DEBUG_PRINT("%s:%d: %d objects occupying %d words between %p and %p\n",
                __FUNCTION__, __LINE__, moves.length(),
                occupied_memory.bit_count(),
                MIN2(source_low, target_low),
                MAX2(source_high, target_high));
#endif
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

// Can be called by GC threads.
void TsanOopMap::update() {
  //assert_not_at_safepoint();

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

  tty->print("############################ TsanOopMap::do_concurrent_work\n");
  MutexLocker mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
  {
    _oop_map->do_concurrent_work(&moves, &source_low, &source_high,
                                 &target_low, &target_high,
                                 &n_downward_moves);
  }

#if 1
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
        tty->print("##### __tsan_java_move [" PTR_FORMAT ", " PTR_FORMAT  "] -> [" PTR_FORMAT ", " PTR_FORMAT "]\n", (long unsigned int)m.source_begin(), (long unsigned int)m.source_end(), (long unsigned int)m.target_begin(), (long unsigned int)m.target_end());
        __tsan_java_move(m.source_begin(), m.target_begin(), m.n_bytes);
      }
    } else {
      // FIXME: add comments from TsanOopSizeMap::rebuild_oops_map for sorting
      //moves.sort((2 * n_downward_moves > moves.length()) ?
      //              TsanOopMapImpl::lessThan : TsanOopMapImpl::moreThan);

      handle_overlapping_moves(moves, min_low, max_high);
    }
  }
#endif

  Atomic::release_store(&_has_work, false);
  tty->print("################################## TsanOopMap::do_concurrent_work done\n");
}

void TsanOopMap::do_concurrent_work(JavaThread* jt) {
  //update();
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
    tty->print("##### __tsan_java_alloc: " PTR_FORMAT ", " PTR_FORMAT "\n", (long unsigned int)addr, (long unsigned int)addr + size * HeapWordSize);
    __tsan_java_alloc(addr, size * HeapWordSize);
  } else {
    tty->print("+++++ obj not added, already in table: " PTR_FORMAT ", " PTR_FORMAT "\n", (long unsigned int)addr, (long unsigned int)addr + size * HeapWordSize);
  }
}

void TsanOopMap::add_oop(oopDesc *addr) {
  // FIXME: N.B. oop's size field must be init'ed; else addr->size() crashes.
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
