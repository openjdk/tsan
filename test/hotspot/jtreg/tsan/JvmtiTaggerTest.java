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

/* @test JvmtiTaggerTest
 * @summary Test that JVMTI tag system is determined to be non-racy.
 * @library /test/lib
 * @build AbstractLoop AbstractNativeLoop TsanRunner
 * @run main/othervm/native JvmtiTaggerTest
 */

import java.io.IOException;

public class JvmtiTaggerTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(JvmtiTaggerLoopRunner.class,
        "-agentlib:AbstractNativeLoop");
  }
}

class JvmtiTaggerLoopRunner extends AbstractNativeLoop {
  private static native boolean addTagAndReference(Object object);
  private static native boolean iterateOverTags();

  @Override
  protected void run(int loopIndex) {
    // Just create objects for the first ten indices.
    if (loopIndex < 10) {
      Object object = new Object();
      if (!addTagAndReference(object)) {
        throw new RuntimeException("Adding a tag failed...");
      }
    }
  }

  public static void main(String[] args) throws InterruptedException {
    JvmtiTaggerLoopRunner loop = new JvmtiTaggerLoopRunner();
    loop.runInTwoThreads();

    if (!iterateOverTags()) {
      throw new RuntimeException("Didn't iterate correctly.");
    }
  }
}
