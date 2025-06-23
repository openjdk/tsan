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

#include "classfile/tsanIgnoreList.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "memory/universe.hpp"
#include "oops/method.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/safepointVerifiers.hpp"
#include "runtime/sharedRuntime.hpp"
#include "tsan/tsanExternalDecls.hpp"
#include "tsan/tsanOopMap.hpp"
#include "utilities/globalDefinitions.hpp"

jint tsan_init() {
  TsanOopMap::initialize_map();  // This is probably early enough.
  TSAN_RUNTIME_ONLY(
    TsanIgnoreList::init();
    if (__tsan_java_init == NULL) {  // We always need tsan runtime functions.
      vm_shutdown_during_initialization("libtsan cannot be located");
      return JNI_ERR;
    }
    __tsan_java_init((julong)Universe::heap()->reserved_region().start(),
                     (julong)Universe::heap()->reserved_region().byte_size());
  );
  return JNI_OK;
}

void tsan_exit() {
  int status = __tsan_java_fini();
  if (status != 0) {
    vm_direct_exit(status);
  }
  TsanOopMap::destroy();
}

// The type of the callback TSAN passes to __tsan_symbolize_external_ex.
// When __tsan_symbolize_external_ex has found a frame, it calls this callback,
// passing along opaque context and frame's location (function name, file
// where it is defined and line and column numbers). Note that we always pass
// -1 as a column.
typedef void (*AddFrameFunc)(void *ctx, const char *function, const char *file,
                             int line, int column);

static void TsanSymbolizeMethod(Method* m, u2 bci, AddFrameFunc add_frame,
                                void* ctx) {
  char methodname_buf[256];
  char filename_buf[128];

  m->name_and_sig_as_C_string(methodname_buf, sizeof(methodname_buf));
  Symbol* filename = m->method_holder()->source_file_name();
  if (filename != NULL) {
    filename->as_C_string(filename_buf, sizeof(filename_buf));
  } else {
    filename_buf[0] = filename_buf[1] = '?';
    filename_buf[2] = '\0';
  }

  add_frame(
      ctx, methodname_buf, filename_buf, m->line_number_from_bci(bci), -1);
}

extern "C" {
// TSAN calls this to symbolize Java frames.
JNIEXPORT void TsanSymbolize(julong loc,
                             AddFrameFunc add_frame,
                             void *ctx) {
  assert(ThreadSanitizer, "Need -XX:+ThreadSanitizer");

  assert((loc & SharedRuntime::tsan_fake_pc_bit) != 0,
         "TSAN should only ask the JVM to symbolize locations the JVM gave TSAN"
        );

  // Use ThreadInVMfromUnknown to transition to VM state to safely call into
  // Method::checked_resolve_jmethod_id. That avoids assertion on thread state
  // with AccessInternal::check_access_thread_state on JDK debug binary. As
  // TsanSymbolize could be triggered from native or Java code, we can't simply
  // make it a JVM_ENTRY to handle native -> vm state transition.
  ThreadInVMfromUnknown __tiv;

  jmethodID method_id = SharedRuntime::tsan_method_id_from_code_location(loc);
  u2 bci = SharedRuntime::tsan_bci_from_code_location(loc);
  Method *m;
  if (method_id == 0) {
    add_frame(
        ctx, bci == 0 ? "(Generated Stub)" : "(Unknown Method)", NULL, -1, -1);
  } else if ((m = Method::checked_resolve_jmethod_id(method_id)) != NULL) {
    // Find a method by its jmethod_id. May fail if method has vanished since.
    TsanSymbolizeMethod(m, bci, add_frame, ctx);
  } else {
    add_frame(ctx, "(Deleted method)", NULL, -1, -1);
  }
}
}

void TsanRawLockAcquired(const char *file, int line,
                         const volatile void *lock) {
  AnnotateRWLockAcquired(file, line, lock, 1);
}

void TsanRawLockReleased(const char *file, int line,
                         const volatile void *lock) {
  AnnotateRWLockReleased(file, line, lock, 1);
}

void TsanRawLockCreate(const char *file, int line, const volatile void *lock) {
  AnnotateRWLockCreate(file, line, lock);
}

void TsanRawLockDestroy(const char *file, int line, const volatile void *lock) {
  AnnotateRWLockDestroy(file, line, lock);
}
