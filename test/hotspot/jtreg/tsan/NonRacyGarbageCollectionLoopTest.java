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

/* @test NonRacyGarbageCollectionLoopTest
 * @summary Test that objects are tracked correctly when garbage collection happens.
 * @library /test/lib
 * @requires vm.gc == null
 * @build AbstractLoop TsanRunner
 * @run main/othervm -XX:+UseParallelGC NonRacyGarbageCollectionLoopTest
 * @run main/othervm -XX:+UseG1GC NonRacyGarbageCollectionLoopTest
 * @run main/othervm -XX:+UseConcMarkSweepGC NonRacyGarbageCollectionLoopTest
 * @run main/othervm -XX:+UseSerialGC NonRacyGarbageCollectionLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class NonRacyGarbageCollectionLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(NonRacyGarbageCollectionLoopRunner.class, "-Xms40m", "-Xmx40m");
  }
}

class NonRacyGarbageCollectionLoopRunner extends AbstractLoop {
  private static final int MEG = 1024 * 1024;
  // NOTE: If HEAP_SIZE changes, make sure also change -Xms and -Xmx values above.
  private static final int HEAP_SIZE = 40 * MEG;
  // Allocate byte arrays that sum up to about 4 times of heap size,
  // assuming 2 threads and each allocates AbstractLoop.LOOPS times.
  // Thus garbage collection must have happened and the same address
  // will be reused to allocate a new object.
  private static final int BYTE_ARRAY_LENGTH = HEAP_SIZE * 4 / 2 / AbstractLoop.LOOPS;

  private volatile byte[] array;

  @Override
  protected void run(int i) {
    byte[] arr = new byte[BYTE_ARRAY_LENGTH];
    for (int j = 0; j < BYTE_ARRAY_LENGTH; j++) {
      arr[j] = 42;
    }
    array = arr;
  }

  public static void main(String[] args) throws InterruptedException {
    NonRacyGarbageCollectionLoopRunner loop = new NonRacyGarbageCollectionLoopRunner();
    loop.runInTwoThreads();
    System.out.println("array[0] = " + loop.array[0]);
  }
}
