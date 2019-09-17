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

/* @test RacyUnsafePutShortLoopTest
 * @summary Test a racy sun.misc.Unsafe.putShort.
 * @library /test/lib
 * @build AbstractLoop TsanRunner UnsafeUtil
 * @run main RacyUnsafePutShortLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class RacyUnsafePutShortLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectFailure(RacyUnsafePutShortLoopRunner.class)
        .shouldMatch("(Read|Write) of size 2 at 0x[0-9a-fA-F]+ by thread T[0-9]+")
        .shouldContain(" #0 RacyUnsafePutShortLoopRunner.run(I)V RacyUnsafePutShortLoopTest.java:")
        .shouldContain(" #0 (Generated Stub) <null>")
        .shouldContain(" #1 sun.misc.Unsafe.putShort(Ljava/lang/Object;JS)V Unsafe.java:");
  }
}

class RacyUnsafePutShortLoopRunner extends AbstractLoop {
  private short x = 0;
  private static final long X_OFFSET = UnsafeUtil.objectFieldOffset(RacyUnsafePutShortLoopRunner.class, "x");

  @Override
  protected synchronized void run(int i) {
    x = (short) (x + 1);
  }

  @Override
  protected void run2(int i) {
    short s;
    synchronized (this) {
      s = this.x;
    }
    UnsafeUtil.UNSAFE.putShort(this, X_OFFSET, (short) (s + 1));
  }

  public static void main(String[] args) throws InterruptedException {
    RacyUnsafePutShortLoopRunner loop = new RacyUnsafePutShortLoopRunner();
    loop.runInTwoThreads();
    System.out.println("x = " + loop.x);
  }
}
