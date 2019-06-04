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

/* @test RacyBooleanMemberLoopTest
 * @summary Test a simple Java data race via a boolean field.
 * @library /test/lib
 * @build AbstractLoop TsanRunner
 * @run main RacyBooleanMemberLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class RacyBooleanMemberLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectFailure(RacyBooleanMemberLoopRunner.class)
        .shouldMatch("(Read|Write) of size 1 at 0x[0-9a-fA-F]+ by thread T[0-9]+")
        .shouldContain(" #0 RacyBooleanMemberLoopRunner.run(I)V RacyBooleanMemberLoopTest.java:");
  }
}

class RacyBooleanMemberLoopRunner extends AbstractLoop {
  private boolean x = false;

  @Override
  protected void run(int i) {
    x = !x;
  }

  public static void main(String[] args) throws InterruptedException {
    RacyBooleanMemberLoopRunner loop = new RacyBooleanMemberLoopRunner();
    loop.runInTwoThreads();
    System.out.println("x = " + loop.x);
  }
}
