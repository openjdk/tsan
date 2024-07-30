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

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import jdk.test.lib.management.InputArguments;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

/**
 * Tsan Test Runner, which basically adds the VM options and runs a class name as a new
 * ProcessBuilder; returning the OutputAnalyzer of the process.
 */
public class TsanRunner {
  public static OutputAnalyzer runTsanTest(Class<?> klass, String... vmArgs) throws IOException {
    ArrayList<String> vmOpts = new ArrayList<>();

    // Pass all VM options passed to this process, which include
    // all JTREG's system properties, and VM options from "test.vm.opts"
    // system property and from @run tag.
    String[] vmInputArgs = InputArguments.getVmInputArgs();
    Collections.addAll(vmOpts, vmInputArgs);

    Collections.addAll(vmOpts, vmArgs);
    vmOpts.add("-XX:+ThreadSanitizer");
    vmOpts.add(klass.getName());

    ProcessBuilder pb =
        ProcessTools.createLimitedTestJavaProcessBuilder(vmOpts.toArray(new String[0]));
    return new OutputAnalyzer(pb.start());
  }

  public static OutputAnalyzer runTsanTestExpectSuccess(
      Class<?> klass, String... vmArgs) throws IOException {
    return runTsanTest(klass, vmArgs)
        .shouldHaveExitValue(0)
        .shouldNotContain("WARNING: ThreadSanitizer: data race");
  }

  public static OutputAnalyzer runTsanTestExpectFailure(
      Class<?> klass, String... vmArgs) throws IOException {
    return runTsanTest(klass, vmArgs)
        .shouldHaveExitValue(66);
  }

  public static OutputAnalyzer runTsanTestExpectSuccess(Class<?> klass) throws IOException {
    return runTsanTestExpectSuccess(klass, new String[0]);
  }

  public static OutputAnalyzer runTsanTestExpectFailure(Class<?> klass) throws IOException {
    return runTsanTestExpectFailure(klass, new String[0]);
  }
}
