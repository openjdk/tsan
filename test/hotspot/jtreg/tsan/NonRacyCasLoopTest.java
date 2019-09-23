/*
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2019 Google and/or its affiliates. All rights reserved.
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
 */

/* @test NonRacyCasLoopTest
 * @summary Test that TSAN properly sees Unsafe CAS.
 * @library /test/lib
 * @build AbstractLoop TsanRunner UnsafeUtil
 * @run main NonRacyCasLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class NonRacyCasLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(NonRacyCasLoopRunner.class);
  }
}

class NonRacyCasLoopRunner extends AbstractLoop {
  int intVar = 0;
  long longVar = 0;
  Object objVar = null;
  int x, y, z;

  static final long INT_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyCasLoopRunner.class, "intVar");
  static final long LONG_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyCasLoopRunner.class, "longVar");
  static final long OBJ_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyCasLoopRunner.class, "objVar");

  @Override
  public void run(int i) {
    while (!UnsafeUtil.UNSAFE.compareAndSwapInt(this, INT_VAR_OFFSET, 0, 1)) {
      ;
    }
    x = x + 1;
    UnsafeUtil.UNSAFE.compareAndSwapInt(this, INT_VAR_OFFSET, 1, 0);

    while (!UnsafeUtil.UNSAFE.compareAndSwapLong(this, LONG_VAR_OFFSET, 0, 1)) {
      ;
    }
    y = y + 1;
    UnsafeUtil.UNSAFE.compareAndSwapLong(this, LONG_VAR_OFFSET, 1, 0);

    Object foo = new Object();
    while (!UnsafeUtil.UNSAFE.compareAndSwapObject(this, OBJ_VAR_OFFSET, null, foo)) {
      ;
    }
    z = z + 1;
    UnsafeUtil.UNSAFE.compareAndSwapObject(this, OBJ_VAR_OFFSET, foo, null);
  }

  public static void main(String[] args) throws InterruptedException {
    NonRacyCasLoopRunner loop = new NonRacyCasLoopRunner();
    loop.runInTwoThreads();
  }
}
