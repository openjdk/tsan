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

/*
 * @test LazyInitReferenceLoopTest
 * @summary Test that accessing the member variables of a lazily initialized
 *          reference field annotated with
 *          java.util.concurrent.annotation.LazyInit does not cause TSAN to
 *          complain.
 * @library /test/lib
 * @build AbstractLoop TsanRunner
 * @run main LazyInitReferenceLoopTest
 */

import java.io.IOException;
import java.util.concurrent.annotation.LazyInit;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class LazyInitReferenceLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(LazyInitReferenceLoopRunner.class);
  }
}

class LazyInitReferenceLoopRunner extends AbstractLoop {
  static class Foo {
    int bar;
  }
  @LazyInit
  Foo foo;

  @Override
  public void run(int i) {
    foo = null;
    Foo f = foo;
    if (f == null) {
      f = new Foo();
      f.bar = 99;
      foo = f;
    }
    int ignore = f.bar;
    if (i == 0) {
      // We don't want to print on every iteration,
      // but also don't want the compiler to remove the read of f.bar.
      System.out.println("ignore: " + ignore);
    }
  }

  public static void main(String[] args) throws InterruptedException {
    LazyInitReferenceLoopRunner loop = new LazyInitReferenceLoopRunner();
    loop.runInTwoThreads();
  }
}
