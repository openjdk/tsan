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
#include "gc/shared/referenceProcessor.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/safepointVerifiers.hpp"
#include "tsan/tsanExternalDecls.hpp"
#include "tsan/tsanOopMap.hpp"
#include "utilities/bitMap.inline.hpp"

extern "C" int jio_printf(const char *fmt, ...);

#if 0
#define DEBUG_PRINT(...) jio_printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif
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

  // Our data
  class TsanOopSizeMap *oop_map = NULL;

  /**
   * TsanOopSizeMap is a hash map of {oopDesc * -> size}.
   */
  class TsanOopSizeMap : public CHeapObj<mtInternal> {

    class TsanOop : public CHeapObj<mtInternal> {
      /* We track the lifecycle (alloc/move/free) of interesting oops;
       * tsan needs to know. */
      oopDesc *_oop;  // key

      /* We cache the oop's size, since we cannot reliably determine it after
       * the oop is freed. Size is measured in number of HeapWords. */
      uintx _oop_size;  // value

    public:
      TsanOop():_oop(NULL), _oop_size(0) {}
      void set_oop(oopDesc *o, uintx s) { _oop = o; _oop_size = s; }
      bool has_oop() const { return _oop != NULL; }
      oopDesc *get_oop() const { return _oop; }
      uintx get_oop_size() const { return _oop_size; }
    };

    size_t _size;
    size_t _n_elements;
    float _load_factor;
    TsanOop *_buckets;

    static uintx _hash64(uintx key) {
      key = ~key + (key << 21);
      key ^= (key >> 24);
      key += (key << 3) + (key << 8);
      key ^= (key >> 14);
      key += (key << 2) + (key << 4);
      key ^= (key >> 28);
      key += (key << 31);
      return key;
    }

    static uintx _hash32(uintx key) {
      key = ~key + (key << 15);
      key ^= (key >> 12);
      key += (key << 2);
      key ^= (key >> 4);
      key *= 2057;
      key ^= (key >> 16);
      return key;
    }

    TsanOop* find_bucket(oopDesc* o) {
      uintx h = reinterpret_cast<uintx>((address)o);
      TsanOop* bucket;
      do {
        h = hash(h);
        bucket = &_buckets[h % _size];
      } while (bucket->has_oop() && bucket->get_oop() != o);
      return bucket;
    }

    static bool collect_oops(BoolObjectClosure* is_alive,
                             OopClosure* f,
                             GrowableArray<PendingMove>* moves,
                             int* n_downward_moves,
                             char** min_low,
                             char** max_high);

    static void handle_overlapping_moves(GrowableArray<PendingMove>& moves,
                                         char* min_low,
                                         char* max_high);

  public:
    TsanOopSizeMap(size_t initial_size)
        : _size(initial_size), _n_elements(0), _load_factor(0.7) {
      _buckets = new TsanOop[_size];
    }

    ~TsanOopSizeMap() {
      delete [] _buckets;
    }

    static uintx hash(uintx key) {
      return (sizeof(uintx) == 4) ? _hash32(key) : _hash64(key);
    }

    // Put an oop and oop size into the hash map.
    // Ok to call multiple times on same oop.
    // Return true if seen for first time; else return false.
    // Synchronized in mutator threads with TsanOopMap_lock.
    bool put(oopDesc* o, uintx s) {
      TsanOop* bucket = find_bucket(o);

      if (!bucket->has_oop()) {
        if (++_n_elements > _load_factor * _size) {
          grow();
          bucket = find_bucket(o);
        }
        bucket->set_oop(o, s);
        return true;
      } else {
        assert(s == bucket->get_oop_size(), "same oop should have same size");
        return false;
      }
    }

    void grow(void) {
      TsanOop *old_buckets = _buckets;
      size_t old_size = _size;
      _size *= 2;

      _buckets = new TsanOop[_size];

      for (uintx i = 0; i < old_size; i++) {
        if (old_buckets[i].has_oop()) {
          put(old_buckets[i].get_oop(), old_buckets[i].get_oop_size());
        }
      }
      delete [] old_buckets;
    }

    // Call this function at the end of the garbage collection to
    // notify TSan about object location changes and to build oops map.
    static void rebuild_oops_map(BoolObjectClosure *is_alive,
                                 OopClosure *pointer_adjuster);

#ifdef ASSERT
    bool exists(oopDesc *o) const {
      uintx h = reinterpret_cast<uintx>((address)o);
      TsanOop *bucket = NULL;

      do {
        h = hash(h);
        bucket = &_buckets[h % _size];
      } while (bucket->has_oop() && bucket->get_oop() != o);

      return bucket->has_oop() && bucket->get_oop() == o;
    }
#endif

    size_t size() const { return _size; }
    oopDesc *oop_at(size_t i) const { return _buckets[i].get_oop(); }
    uintx oop_size_at(size_t i) const { return _buckets[i].get_oop_size(); }
  };

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
  // TsanOopSizeMap::rebuild_oop_map below uses an instance of this
  // class to order object moves, please see additional comments there.
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

  bool TsanOopSizeMap::collect_oops(BoolObjectClosure* is_alive,
                                    OopClosure* pointer_adjuster,
                                    GrowableArray<PendingMove>* moves,
                                    int* n_downward_moves,
                                    char** min_low,
                                    char** max_high) {
    size_t map_size = oop_map->size();

    // Traverse oop map. For each object that survived GC calculate its new
    // oop, add it to the new oop map, and append the move from the source oop
    // to the target one to the moves list. While doing that, collect oop
    // source and target ranges and count the moves that move an object
    // downwards (this is heuristics to order the moves, see below).
    TsanOopSizeMap* new_map = new TsanOopSizeMap(map_size / 2);
    *n_downward_moves = 0;
    bool disjoint_regions;
    char *source_low = reinterpret_cast<char *>(UINTPTR_MAX);
    char *source_high = NULL;
    char *target_low = reinterpret_cast<char *>(UINTPTR_MAX);
    char *target_high = NULL;
    size_t deleted_objects = 0;
    size_t unmoved_objects = 0;
    size_t total_size_words = 0;
    CollectedHeap *heap = Universe::heap();
    for (size_t i = 0; i < map_size; i++) {
      oopDesc *source_obj = oop_map->oop_at(i);

      if (source_obj != NULL && heap->is_in_reserved(source_obj)) {
        uintx obj_size = oop_map->oop_size_at(i);
        size_t obj_size_bytes = obj_size * HeapWordSize;
        if (is_alive->do_object_b(source_obj)) {
          // The object survived GC, add its updated oop to the new oops map.
          oop target_oop = cast_to_oop((intptr_t)source_obj);
          pointer_adjuster->do_oop(&target_oop);
          // The memory pointed by target_oop may not be a valid oop yet,
          // for example the G1 full collector needs to adjust all pointers
          // first, then compacts and moves the objects. In this case
          // TsanOopSizeMap::rebuild_oops_map() is called during the adjust-
          // pointer phase, before the collector moves the objects. Thus,
          // we cannot use heap->is_in() or oopDesc::is_oop() to check
          // target_oop.
          assert(heap->is_in_reserved(target_oop), "Adjustment failed");
          oopDesc *target_obj = target_oop;
          new_map->put(target_obj, obj_size);
          if (target_obj == source_obj) {
            ++unmoved_objects;
            continue;
          }
          if (target_obj < source_obj) {
            ++(*n_downward_moves);
          }
          // Append to the moves list.
          PendingMove move = {(char *)source_obj, (char *)target_obj,
                              obj_size_bytes};
          total_size_words += obj_size;
          moves->append(move);

          // Update source and target ranges.
          source_low = MIN2(source_low, move.source_begin());
          source_high = MAX2(source_high, move.source_end());
          target_low = MIN2(target_low, move.target_begin());
          target_high = MAX2(target_high, move.target_end());
        } else {  // dead!
          __tsan_java_free((char *)source_obj, obj_size_bytes);
          ++deleted_objects;
        }
      }
    }

    // Update the oop map.
    delete TsanOopMapImpl::oop_map;
    TsanOopMapImpl::oop_map = new_map;

    disjoint_regions = (source_low >= target_high || source_high <= target_low);
    log_debug(gc)(
          "Tsan: map of " SIZE_FORMAT " objects, " SIZE_FORMAT " deleted, "
          SIZE_FORMAT " unmoved, " SIZE_FORMAT " to move "
          "(" SIZE_FORMAT " words), %soverlap",
          map_size, deleted_objects, unmoved_objects, (size_t)moves->length(),
          total_size_words, disjoint_regions ? "no " : "");

    *min_low = MIN2(source_low, target_low);
    *max_high = MAX2(source_high, target_high);
    return disjoint_regions;
  }

  void TsanOopSizeMap::handle_overlapping_moves(GrowableArray<PendingMove>& moves,
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

  void TsanOopSizeMap::rebuild_oops_map(BoolObjectClosure *is_alive,
                                        OopClosure *pointer_adjuster) {
    ResourceMark rm;
    GCTraceTime(Debug, gc) tt_top("Tsan relocate");
    GCTraceCPUTime tcpu;
    GrowableArray<PendingMove> moves(MAX2((int)(oop_map->size() / 100),
                                          100000));
    bool disjoint_regions;
    int n_downward_moves;
    char *min_low, *max_high;

    {
      GCTraceTime(Debug, gc) tt_collect("Collect oops");
      disjoint_regions = collect_oops(is_alive, pointer_adjuster, &moves,
                                      &n_downward_moves, &min_low, &max_high);
    }
    if (moves.length() == 0) {
      return;
    }

    // Notifying TSan is straightforward when source and target regions
    // do not overlap:
    if (disjoint_regions) {
      GCTraceTime(Debug, gc) tt_disjoint("Move between regions");

      for (int i = 0; i < moves.length(); ++i) {
        const PendingMove &m = moves.at(i);
        __tsan_java_move(m.source_begin(), m.target_begin(), m.n_bytes);
      }
      return;
    }

    // Source and target ranges overlap, the moves need to be ordered to prevent
    // overwriting. Overall, this can take N^2 steps if only one object can be
    // moved during the array traversal; however, when we are dealing with
    // compacting garbage collector, observation shows that the overwhelming
    // majority of the objects move in one direction. If we sort the moves (in
    // the ascending order if dominant direction is downwards, in the descending
    // order otherwise), chances are we will be able to order the moves in a few
    // traversals of the moves array.
    {
      GCTraceTime(Debug, gc) tt_sort("Sort moves");

      moves.sort((2 * n_downward_moves > moves.length()) ? lessThan : moreThan);
      log_debug(gc)("Tsan: sort %d objects", moves.length());
    }

    {
      GCTraceTime(Debug, gc) tt_sort("Move");
      handle_overlapping_moves(moves, min_low, max_high);
    }
  }

}  // namespace TsanOopMapImpl


void TsanOopMap::initialize_map() {
  TsanOopMapImpl::oop_map = new TsanOopMapImpl::TsanOopSizeMap(512);
}

void TsanOopMap::destroy() {
  delete TsanOopMapImpl::oop_map;
}

void TsanOopMap::weak_oops_do(
    BoolObjectClosure* is_alive,
    OopClosure* pointer_adjuster) {
  if (!ThreadSanitizer) return;
  assert(SafepointSynchronize::is_at_safepoint(), "must be at a safepoint");

  // We're mutating oopMap, but we don't need to acquire TsanOopMap_lock:
  // Mutation to map happens at (A) constructor (single threaded) and
  // (B) add (in mutator threads) and (C) do_weak_oops (single-threaded).
  // Calls between add are synchronized.
  // Calls between add and do_weak_oops are synchronized via STW GC.
  TsanOopMapImpl::TsanOopSizeMap::rebuild_oops_map(
      is_alive, pointer_adjuster);
}

// Safe to deal with raw oop; for example this is called in a LEAF function
// There is no safepoint in this code: 1) special mutex is used, and
// 2) there is no VM state transition
// We cannot use ordinary VM mutex, as that requires a state transition.
void TsanOopMap::add_oop_with_size(oopDesc *addr, int size) {
  DEBUG_ONLY(NoSafepointVerifier nsv;)
  assert(TsanOopMapImpl::oop_map != NULL, "TsanOopMap not initialized");
  guarantee(addr != NULL, "null oop");
  bool alloc = false;
  {
    MutexLockerEx mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    // N.B. addr->size() may not be available yet!
    alloc = TsanOopMapImpl::oop_map->put(addr, size);
  }
  if (alloc) {
    __tsan_java_alloc(addr, size * HeapWordSize);
  }
}

void TsanOopMap::add_oop(oopDesc *addr) {
  // N.B. oop's size field must be init'ed; else addr->size() crashes.
  TsanOopMap::add_oop_with_size(addr, addr->size());
}

#ifdef ASSERT
bool TsanOopMap::exists(oopDesc *addr) {
  DEBUG_ONLY(NoSafepointVerifier nsv;)
  assert(TsanOopMapImpl::oop_map != NULL, "TsanOopMap not initialized");
  guarantee(addr != NULL, "null oop");
  bool in_map = false;
  {
    MutexLockerEx mu(TsanOopMap_lock, Mutex::_no_safepoint_check_flag);
    in_map = TsanOopMapImpl::oop_map->exists(addr);
  }
  return in_map;
}
#endif
