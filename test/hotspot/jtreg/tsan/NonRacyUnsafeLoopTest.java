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

/* @test NonRacyUnsafeLoopTest
 * @summary Test non-racy Unsafe accesses.
 * @library /test/lib
 * @build AbstractLoop TsanRunner UnsafeUtil
 * @run main NonRacyUnsafeLoopTest
 */

import java.io.IOException;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class NonRacyUnsafeLoopTest {
  public static void main(String[] args) throws IOException {
    TsanRunner.runTsanTestExpectSuccess(NonRacyUnsafeLoopRunner.class);
  }
}

class NonRacyUnsafeLoopRunner extends AbstractLoop {
  byte byteVar = 0x42;
  char charVar = 'a';
  short shortVar = 10;
  int intVar = 11;
  long longVar = 12;
  float floatVar = 1.0f;
  double doubleVar = 2.0;
  String stringVar = "f";

  static final long BYTE_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyUnsafeLoopRunner.class, "byteVar");
  static final long CHAR_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyUnsafeLoopRunner.class, "charVar");
  static final long SHORT_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyUnsafeLoopRunner.class, "shortVar");
  static final long INT_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyUnsafeLoopRunner.class, "intVar");
  static final long LONG_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyUnsafeLoopRunner.class, "longVar");
  static final long FLOAT_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyUnsafeLoopRunner.class, "floatVar");
  static final long DOUBLE_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyUnsafeLoopRunner.class, "doubleVar");
  static final long STRING_VAR_OFFSET =
      UnsafeUtil.objectFieldOffset(NonRacyUnsafeLoopRunner.class, "stringVar");

  @Override
  public synchronized void run(int i) {
    byteVar = (byte) (byteVar + 1);
    charVar = (char) (charVar + 1);
    shortVar = (short) (charVar + 1);
    intVar = intVar + 1;
    longVar = (long) (longVar + 2);
    floatVar = (float) (floatVar + 0.1);
    doubleVar = (double) (doubleVar + 0.01);
    char c = (char) (stringVar.charAt(0) + 1);
    stringVar = Character.toString(c);
  }

  @Override
  public synchronized void run2(int i) {
    UnsafeUtil.UNSAFE.putByte(this, BYTE_VAR_OFFSET, (byte) (UnsafeUtil.UNSAFE.getByte(this, BYTE_VAR_OFFSET) + 1));
    UnsafeUtil.UNSAFE.putChar(this, CHAR_VAR_OFFSET, (char) (UnsafeUtil.UNSAFE.getChar(this, CHAR_VAR_OFFSET) + 1));
    UnsafeUtil.UNSAFE.putShort(this, SHORT_VAR_OFFSET, (short) (UnsafeUtil.UNSAFE.getShort(this, SHORT_VAR_OFFSET) + 2));
    UnsafeUtil.UNSAFE.putInt(this, INT_VAR_OFFSET, (int) (UnsafeUtil.UNSAFE.getInt(this, INT_VAR_OFFSET) + 3));
    UnsafeUtil.UNSAFE.putLong(this, LONG_VAR_OFFSET, (long) (UnsafeUtil.UNSAFE.getLong(this, LONG_VAR_OFFSET) + 4));
    UnsafeUtil.UNSAFE.putFloat(
        this, FLOAT_VAR_OFFSET, (float) (UnsafeUtil.UNSAFE.getFloat(this, FLOAT_VAR_OFFSET) + 0.1));
    UnsafeUtil.UNSAFE.putDouble(
        this, DOUBLE_VAR_OFFSET, (double) (UnsafeUtil.UNSAFE.getDouble(this, DOUBLE_VAR_OFFSET) + 0.2));
    char c = (char) (((String) UnsafeUtil.UNSAFE.getObject(this, STRING_VAR_OFFSET)).charAt(0) + 1);
    UnsafeUtil.UNSAFE.putObject(this, STRING_VAR_OFFSET, Character.toString(c));
  }

  public static void main(String[] args) throws InterruptedException {
    NonRacyUnsafeLoopRunner loop = new NonRacyUnsafeLoopRunner();
    loop.runInTwoThreads();
  }
}
