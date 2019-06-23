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

/* @test StringOperationsLoopTest
 * @summary Test that String concatenation/hashCode is not reported as racy.
 *          TSAN may see String concatentation as racy due to usage of
 *          java.lang.invoke.*, which is racy. Those races should be
 *          suppressed.
 *          hashCode() is intentionally racy and should not be reported.
 * @library /test/lib
 * @build AbstractLoop TsanRunner
 * @run main StringOperationsLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class StringOperationsLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(StringOperationsLoopTestRunner.class);
  }
}

class StringOperationsLoopTestRunner extends AbstractLoop {
  public static final String STRING = "hi";

  @Override
  protected void run(int i) {
    int a = 0;
    String b = "" + a;
    STRING.hashCode();
  }

  public static void main(String[] args) throws InterruptedException {
    StringOperationsLoopTestRunner loop = new StringOperationsLoopTestRunner();
    loop.runInTwoThreads();
  }
}
