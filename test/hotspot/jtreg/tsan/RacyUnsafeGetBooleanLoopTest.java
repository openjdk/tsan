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

/* @test RacyUnsafeGetBooleanLoopTest
 * @summary Test a racy sun.misc.Unsafe.getBoolean.
 * @library /test/lib
 * @build AbstractLoop TsanRunner UnsafeUtil
 * @run main RacyUnsafeGetBooleanLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class RacyUnsafeGetBooleanLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectFailure(RacyUnsafeGetBooleanLoopRunner.class)
        .shouldMatch("(Read|Write) of size 1 at 0x[0-9a-fA-F]+ by thread T[0-9]+")
        .shouldContain(" #0 RacyUnsafeGetBooleanLoopRunner.run(I)V RacyUnsafeGetBooleanLoopTest.java:")
        .shouldContain(" #0 (Generated Stub) <null>")
        .shouldContain(" #1 sun.misc.Unsafe.getBoolean(Ljava/lang/Object;J)Z Unsafe.java:");
  }
}

class RacyUnsafeGetBooleanLoopRunner extends AbstractLoop {
  private boolean x = false;
  private static final long X_OFFSET = UnsafeUtil.objectFieldOffset(RacyUnsafeGetBooleanLoopRunner.class, "x");

  @Override
  protected synchronized void run(int i) {
    x = !x;
  }

  @Override
  protected void run2(int i) {
    boolean x = !UnsafeUtil.UNSAFE.getBoolean(this, X_OFFSET);
    synchronized (this) {
      this.x = x;
    }
  }

  public static void main(String[] args) throws InterruptedException {
    RacyUnsafeGetBooleanLoopRunner loop = new RacyUnsafeGetBooleanLoopRunner();
    loop.runInTwoThreads();
    System.out.println("x = " + loop.x);
  }
}
