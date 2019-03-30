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

#include "gc/shared/collectedHeap.hpp"
#include "memory/universe.hpp"
#include "runtime/java.hpp"
#include "tsan/tsanExternalDecls.hpp"
#include "utilities/globalDefinitions.hpp"

jint tsan_init() {
  if (__tsan_java_init == NULL) {  // We always need tsan runtime functions.
    vm_shutdown_during_initialization("libtsan cannot be located");
    return JNI_ERR;
  }
  __tsan_java_init((julong)Universe::heap()->reserved_region().start(),
                   (julong)Universe::heap()->reserved_region().byte_size());
  return JNI_OK;
}

void tsan_exit() {
  int status = __tsan_java_fini();
  if (status != 0) {
    vm_direct_exit(status);
  }
}

// The type of the callback TSAN passes to __tsan_symbolize_external_ex.
// When __tsan_symbolize_external_ex has found a frame, it calls this callback,
// passing along opaque context and frame's location (function name, file
// where it is defined and line and column numbers). Note that we always pass
// -1 as a column.
typedef void (*AddFrameFunc)(void *ctx, const char *function, const char *file,
                             int line, int column);

// TSAN calls this to symbolize Java frames.
// This is not in tsanExternalDecls.hpp because this is a function that the JVM
// is supposed to override which TSAN will call, not a TSAN function that the
// JVM calls.
extern "C" void __tsan_symbolize_external_ex(julong pc,
                                             AddFrameFunc addFrame,
                                             void *ctx) {
}

