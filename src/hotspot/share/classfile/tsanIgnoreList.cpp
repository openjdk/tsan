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

#include "precompiled.hpp"
#include "classfile/tsanIgnoreList.hpp"
#include "classfile/symbolTable.hpp"
#include "memory/resourceArea.inline.hpp"

static const int MAX_LINE_SIZE  = 1024;

class FieldMatcher : public CHeapObj<mtClass> {
 public:
  enum Mode {
    Exact = 0,
    Prefix = 1,
    Any = 2,
    Unknown = -1
  };

  FieldMatcher(const Symbol* class_name, Mode class_mode,
               const Symbol* field_name, Mode field_mode, FieldMatcher* next)
      : _class_name(class_name),
        _field_name(field_name),
        _class_mode(class_mode),
        _field_mode(field_mode),
        _next(next) { }

  // Given a FieldMatcher as the head of linked-list, returns true if any
  // FieldMatcher in the list matches.
  static bool match_any(FieldMatcher* head,
                        const Symbol* class_name,
                        const Symbol* field_name) {
    while (head) {
      if (head->match(class_name, field_name)) {
        return true;
      }
      head = head->_next;
    }
    return false;
  }

 protected:
  const Symbol* _class_name;
  const Symbol* _field_name;
  Mode _class_mode;
  Mode _field_mode;
  FieldMatcher* _next;

  static bool match(const Symbol* candidate, const Symbol* match, Mode mode) {
    ResourceMark rm;
    switch (mode) {
      case Exact:
        return candidate == match;
      case Prefix: {
        const char* candidate_str = candidate->as_C_string();
        const char* match_str = match->as_C_string();
        return (strstr(candidate_str, match_str) == candidate_str);
      }
      case Any:
        return true;
      default:
        return false;
    }
  }

  bool match(const Symbol* class_name, const Symbol* field_name) {
    return (match(class_name, _class_name, _class_mode) &&
            match(field_name, _field_name, _field_mode));
  }
};

FieldMatcher* TsanIgnoreList::_exact_match = NULL;
FieldMatcher* TsanIgnoreList::_prefix_match = NULL;

// Detects the pattern-matching mode based on the presence and location of
// wildcard character, fixes the pattern inplace and returns the
// pattern-matching mode.
static FieldMatcher::Mode make_pattern(char* pattern) {
  const int len = strlen(pattern);
  // Inverse of Symbol::as_klass_external_name.
  // Turn all '.'s into '/'s.
  for (int index = 0; index < len; index++) {
    if (pattern[index] == '.') {
      pattern[index] = '/';
    }
  }

  char* asterisk = strstr(pattern, "*");
  if (asterisk == NULL) {
    return FieldMatcher::Exact;
  }
  if (asterisk - pattern != len - 1) {
    warning("Unexpected location for '*' in \"%s\". "
            "Only prefix patterns are supported.", pattern);
  }
  if (asterisk == pattern) {
    return FieldMatcher::Any;
  }
  pattern[len - 1] = '\0';
  return FieldMatcher::Prefix;
}

void TsanIgnoreList::parse_from_line(char* line) {
  EXCEPTION_MARK;
  char class_pattern[MAX_LINE_SIZE], field_pattern[MAX_LINE_SIZE];
  // Replace '#' with '\0'.
  {
    char* comment = strchr(line, '#');
    if (comment != NULL) {
      *comment = '\0';
    }
  }
  // Parse line.
  if (sscanf(line, "%s %s", class_pattern, field_pattern) != 2) {
    return;
  }
  // Get matcher mode from pattern.
  FieldMatcher::Mode class_mode = make_pattern(class_pattern);
  FieldMatcher::Mode field_mode = make_pattern(field_pattern);
  // If we match against Any, no need for a symbol, else create the symbol.
  Symbol* class_symbol = (class_mode == FieldMatcher::Any) ? NULL :
      SymbolTable::new_symbol(class_pattern, CHECK);
  Symbol* field_symbol = (field_mode == FieldMatcher::Any) ? NULL :
      SymbolTable::new_symbol(field_pattern, CHECK);
  // Add matcher to beginning of linked list.
  if (class_mode == FieldMatcher::Exact && field_mode == FieldMatcher::Exact) {
    _exact_match = new FieldMatcher(class_symbol, class_mode, field_symbol,
                                    field_mode, _exact_match);
  } else {
    _prefix_match = new FieldMatcher(class_symbol, class_mode, field_symbol,
                                     field_mode, _prefix_match);
  }
}

void TsanIgnoreList::parse_from_file(FILE* stream) {
  char line[MAX_LINE_SIZE];
  while (fgets(line, sizeof(line), stream)) {
    if (strlen(line) == sizeof(line) - 1) {
      warning("TSAN ignore file (ThreadSanitizerIgnoreFile) contains a line longer "
              "than %d. This pattern will be truncated, and the rest of the "
              "file will not be processed for pattern matching.",
              MAX_LINE_SIZE);
      break;
    }
    parse_from_line(line);
  }
  if (ferror(stream)) {
    warning("Error reading from TSAN ignore file");
  }
}

void TsanIgnoreList::init() {
  if (ThreadSanitizerIgnoreFile == NULL) {
    return;
  }

  FILE* stream = fopen(ThreadSanitizerIgnoreFile, "rt");
  if (stream == NULL) {
    warning("TSAN ignore file (ThreadSanitizerIgnoreFile:%s) not found.",
            ThreadSanitizerIgnoreFile);
    return;
  }
  parse_from_file(stream);
  fclose(stream);
}

bool TsanIgnoreList::match(
    const Symbol* class_name, const Symbol* field_name,
    BasicType type) {
  // Wildcard matches are only for primitive types. References should be
  // added to list individually since they become release/acquire.
  if (is_java_primitive(type) &&
      FieldMatcher::match_any(_prefix_match, class_name, field_name)) {
    return true;
  }
  return FieldMatcher::match_any(_exact_match, class_name, field_name);
}
