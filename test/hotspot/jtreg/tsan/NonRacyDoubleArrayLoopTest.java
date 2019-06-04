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

/* @test NonRacyDoubleArrayLoopTest
 * @summary Test a simple Java non-racy memory access via a double array.
 * @library /test/lib
 * @build AbstractLoop TsanRunner
 * @run main NonRacyDoubleArrayLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class NonRacyDoubleArrayLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(NonRacyDoubleArrayLoopRunner.class);
  }
}

class NonRacyDoubleArrayLoopRunner extends AbstractLoop {
  private double[] x = new double[2];

  @Override
  protected synchronized void run(int i) {
    x[0] = x[0] + 1.0;
  }

  public static void main(String[] args) throws InterruptedException {
    NonRacyDoubleArrayLoopRunner loop = new NonRacyDoubleArrayLoopRunner();
    loop.runInTwoThreads();
    System.out.println("x = " + loop.x[0]);
  }
}
