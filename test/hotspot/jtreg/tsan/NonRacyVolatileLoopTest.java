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

/* @test NonRacyVolatileLoopTest
 * @summary Test that volatile field accesses properly introduce synchronization.
 * @library /test/lib
 * @build AbstractLoop TsanRunner
 * @run main NonRacyVolatileLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class NonRacyVolatileLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(NonRacyVolatileLoopRunner.class);
  }
}

class NonRacyVolatileLoopRunner extends AbstractLoop {
  private volatile byte b;
  private volatile String s;
  int data1;
  int data2;

  @Override
  protected void syncSetup() {
    b = 1;
    s = "a";
  }

  @Override
  protected void run(int i) {
    data1 = 42;
    b = 2;

    data2 = 43;
    s = "b";
  }

  @Override
  protected void run2(int i) {
    while (b != 2) {
      ;
    }
    int x = data1;
    while (!s.equals("b")) {
      ;
    }
    x = data2;
  }

  public static void main(String[] args) throws InterruptedException {
    NonRacyVolatileLoopRunner loop = new NonRacyVolatileLoopRunner();
    loop.runInTwoThreadsSync();
  }
}
