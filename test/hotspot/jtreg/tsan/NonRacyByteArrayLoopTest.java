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

/* @test NonRacyByteArrayLoopTest
 * @summary Test a simple Java non-racy memory access via a byte array.
 * @library /test/lib
 * @build AbstractLoop TsanRunner
 * @run main NonRacyByteArrayLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class NonRacyByteArrayLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(NonRacyByteArrayLoopRunner.class);
  }
}

class NonRacyByteArrayLoopRunner extends AbstractLoop {
  private byte[] x = new byte[2];

  @Override
  protected synchronized void run(int i) {
    x[0] = (byte) (x[0] + 1);
  }

  public static void main(String[] args) throws InterruptedException {
    NonRacyByteArrayLoopRunner loop = new NonRacyByteArrayLoopRunner();
    loop.runInTwoThreads();
    System.out.println("x = " + loop.x[0]);
  }
}
