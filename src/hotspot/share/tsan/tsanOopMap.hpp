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

#ifndef SHARE_TSAN_TSANOOPMAP_HPP
#define SHARE_TSAN_TSANOOPMAP_HPP

// Forward declarations
class OopStorage;

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

}  // namespace TsanOopMapImpl

#include "tsan/tsanOopMapTable.hpp"

// FIXME!!!
// Interface class to manage oop addresses for ThreadSanitizer.
// TSAN needs to keep track of all allocated Java objects, in order to keep
// TSAN's metadata updated. When an object becomes free or moved, there should
// be a call to __tsan_java_free or __tsan_java_move accordingly.
// FIXME!!! The map is implemented as a hash map of oop address to oop size.
// Oop size must be cached, as it is unsafe to call size() after reference is
// collected.
// Turn it on with -XX:+ThreadSanitizer
//
// Some invariants:
// 1. add_*() is only passed a live oop.
// 2. add_*() must be thread-safe wrt itself.
//    (other functions are not called from a multithreaded context)

class TsanOopMap : public AllStatic {
public:
  // Called by primordial thread to initialize oop mapping.
  static void initialize_map();
  static void destroy();

  //static void set_needs_cleaning();
  static void gc_notification(size_t num_dead_entries);

  // Main operation; must be thread-safe and safepoint-free.
  // Called when an object is used as a monitor.
  // The first time addr is seen, __tsan_java_alloc is called.
  static void add_oop(oopDesc* addr);
  static void add_oop_with_size(oopDesc* addr, int size);

#ifdef ASSERT
  static bool exists(oopDesc* addr);
#endif

  static OopStorage* oop_storage();

  // Used by GC. FIXME
  static void update();

  //static bool has_work();

  //static void trigger_concurrent_work();

  // FIXME
  void release();
};

#endif // SHARE_TSAN_TSANOOPMAP_HPP
