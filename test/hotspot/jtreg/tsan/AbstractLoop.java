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

/**
 * This class forms the basis for a regression suite against Tsan/Java.
 *
 * This class abstracts away the tedium of setting up two threads to run a method in a loop.
 * Usually, we want to run the same method in two threads.
 *
 * Usually, we want to set up a unique piece of shared data and access it in a variety of ways:
 * with synchronization, without, etc.
 *
 * Each subclass will override run().
 */
abstract class AbstractLoop {
  static final int LOOPS = 50000;

  static final Thread.UncaughtExceptionHandler HANDLER = new Thread.UncaughtExceptionHandler() {
    @Override
    public void uncaughtException(Thread th, Throwable ex) {
      System.err.println("Uncaught Exception in thread " + th.getName());
      ex.printStackTrace();
      System.exit(1);
    }
  };

  /**
   * Implement only this method for symmetric behavior.
   */
  protected abstract void run(int i);

  /**
   * Override this method for asymmetric behavior.
   */
  protected void run2(int i) {
    run(i);
  }

  // Threads pulled out to allow direct reference
  final Thread t1 =
      new Thread(
          () -> {
            for (int i = 0; i < LOOPS; i++) {
              AbstractLoop.this.run(i);
            }
          });

  final Thread t2 =
      new Thread(
          () -> {
            for (int i = 0; i < LOOPS; i++) {
              AbstractLoop.this.run2(i);
            }
          });

  final void runInTwoThreads() throws InterruptedException {
    System.err.println("Begin " + name);

    t1.setUncaughtExceptionHandler(HANDLER);
    t2.setUncaughtExceptionHandler(HANDLER);
    t1.start();
    t2.start();

    t1.join();
    t2.join();

    System.err.println("End   " + name);
  }

  private String name;
  // Default constructors will call this
  AbstractLoop() {
    name = this.getClass().getSimpleName();
  }
}
