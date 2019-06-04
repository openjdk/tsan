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

/* @test NonRawRacyNativeLoopTest
 * @summary Test that native code protected by JVMTI synchronization is not racy.
 * @library /test/lib
 * @build AbstractLoop AbstractNativeLoop TsanRunner
 * @run main/othervm/native NonRawRacyNativeLoopTest
 */

import java.io.IOException;

public class NonRawRacyNativeLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(NonRawRacyNativeLoopRunner.class,
        "-agentlib:AbstractNativeLoop");
  }
}

class NonRawRacyNativeLoopRunner extends AbstractNativeLoop {
  private long lock;

  NonRawRacyNativeLoopRunner() {
    // Both threads will use the same lock, so we should be not racy.
    lock = createRawLock();
  }

  @Override
  protected void run(int i) {
    writeRawLockedNativeGlobal(lock);
  }

  public static void main(String[] args) throws InterruptedException {
    NonRawRacyNativeLoopRunner loop = new NonRawRacyNativeLoopRunner();
    loop.runInTwoThreads();
    System.out.println("native_global = " + loop.readNativeGlobal());
  }
}
