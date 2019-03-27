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

#ifndef SHARE_RUNTIME_TSANEXTERNALDECLS_HPP
#define SHARE_RUNTIME_TSANEXTERNALDECLS_HPP

#include "utilities/globalDefinitions.hpp"

#define WEAK __attribute__((weak))

// These declarations constitute the VM-ThreadSanitizer interface.
// These functions are the only way the VM notifies Tsan about critical events;
// they are "push" functions.
//
// These functions must be declared as "weak" symbols: the function
// definitions are available only when the Tsan runtime is available, such as
// LD_PRELOAD or statically linking libtsan.
extern "C" {
  // Called after Java heap is set up.
  // It must be called before any other __tsan_java_* function.
  void __tsan_java_init(julong heap_begin, julong heap_size) WEAK;
  // Called after Java application exits.
  // It does not have to be the final function called.
  int __tsan_java_fini() WEAK;

  // Called on Java method entry and exit.
  void __tsan_func_entry(void *pc) WEAK;
  void __tsan_func_exit() WEAK;
}

#endif  // SHARE_RUNTIME_TSANEXTERNALDECLS_HPP
