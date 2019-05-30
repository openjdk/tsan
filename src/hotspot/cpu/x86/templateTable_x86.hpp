/*
 * Copyright (c) 1998, 2019, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_X86_TEMPLATETABLE_X86_HPP
#define CPU_X86_TEMPLATETABLE_X86_HPP

  static void prepare_invoke(int byte_no,
                             Register method,         // linked method (or i-klass)
                             Register index = noreg,  // itable index, MethodType, etc.
                             Register recv  = noreg,  // if caller wants to see it
                             Register flags = noreg   // if caller wants to test it
                             );
  static void invokevirtual_helper(Register index, Register recv,
                                   Register flags);
  static void volatile_barrier(Assembler::Membar_mask_bits order_constraint);

  // Helpers
  static void index_check(Register array, Register index);
  static void index_check_without_pop(Register array, Register index);

  static void putfield_or_static_helper(int byte_no, bool is_static, RewriteControl rc,
                                        Register obj, Register off, Register flags);
  static void fast_storefield_helper(Address field, Register rax);

#if INCLUDE_TSAN
  typedef void (*TsanMemoryReleaseAcquireFunction)(void* /* address */);
  typedef void (*TsanMemoryReadWriteFunction)(void* /* address */,
                                              Method* /* method */,
                                              address /* bcp */);

  // The corresponding tsan_acquire/release function for a
  // TsanMemoryReadWriteFunction.
  static TsanMemoryReleaseAcquireFunction tsan_release_acquire_method(
      TsanMemoryReadWriteFunction tsan_function);

  // Tell tsan that a member/static variable has been read from or written to.
  // tsan_function must be one of the SharedRuntime::tsan_read/write*
  // functions.
  // Flags is the register that contains the field cache entry flags bitfield.
  // Because the field may be volatile, for a write, this function must be
  // called before the write; for a read, this function must be called after
  // the read. This way the acquire/release is ordered correctly relative to the
  // read/write.
  static void tsan_observe_get_or_put(
      const Address &field,
      Register flags,
      TsanMemoryReadWriteFunction tsan_function,
      TosState tos);

  // Tell tsan that an array has been read from or written to.
  // tsan_function must be one of the SharedRuntime::tsan_read/write*
  // functions.
  // Unlike tsan_observe_get_or_put(), the ordering relative to the
  // read/write does not matter since array loads/stores are never volatile.
  static void tsan_observe_load_or_store(
      const Address& address,
      TsanMemoryReadWriteFunction tsan_function);

#endif  // INCLUDE_TSAN

#endif // CPU_X86_TEMPLATETABLE_X86_HPP
