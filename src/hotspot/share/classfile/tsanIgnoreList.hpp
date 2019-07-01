/*
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2019, Google and/or its affiliates. All rights reserved.
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
 *
 */

#ifndef SHARE_CLASSFILE_TSANIGNORELIST_HPP
#define SHARE_CLASSFILE_TSANIGNORELIST_HPP

class FieldMatcher;

// Loads a whitelist file (-XX:ThreadSanitizerIgnoreFile) containing class names
// and field names that will be ignored by Java TSAN instrumentation.
// Lines that start with '#' are considered comments.
// Fields with primitive type can be whitelisted with a wildcard prefix match
// for both field name and class name.
// Here are a few examples.
// To whitelist field myBaz in a class named com.foo.Bar
// com.foo.Bar myBaz
//
// Every field with primitive type starting with my in that class:
// com.foo.Bar my*
//
// And every primitive field in package com.foo:
// com.foo.* *
class TsanIgnoreList : AllStatic {
 public:
  static void init();

  // Matches a class name and a field name with the whitelisted patterns.
  // type is the type of the field. Since we use ignored object reference
  // fields as a way to say that the object they point to is also safe to
  // pass around without synchronization, we only match primitive types with
  // wildcard patterns. References need to be whitelisted individually.
  static bool match(const Symbol* class_name, const Symbol* field_name,
                    BasicType type);
 private:
  static void parse_from_line(char* line);
  static void parse_from_file(FILE* stream);

  static FieldMatcher* _prefix_match;
  static FieldMatcher* _exact_match;
};

#endif  // SHARE_CLASSFILE_TSANIGNORELIST_HPP
