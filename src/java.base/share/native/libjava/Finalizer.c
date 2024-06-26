/*
 * Copyright (c) 2021 Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2019 Google and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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

#include "jni.h"
#include "jvm.h"

#include "java_lang_ref_Finalizer.h"

void __tsan_java_finalize() __attribute__((weak));

JNIEXPORT void JNICALL
Java_java_lang_ref_Finalizer_tsanFinalize(JNIEnv *env, jclass unused)
{
  __tsan_java_finalize();
}

JNIEXPORT jboolean JNICALL
Java_java_lang_ref_Finalizer_isTsanEnabled(JNIEnv *env, jclass unused)
{
  return JVM_GetTsanEnabled(env);
}

JNIEXPORT void JNICALL
Java_java_lang_ref_Finalizer_reportComplete(JNIEnv* env, jclass cls, jobject finalizee) {
    JVM_ReportFinalizationComplete(env, finalizee);
}

JNIEXPORT jboolean JNICALL
Java_java_lang_ref_Finalizer_isFinalizationEnabled(JNIEnv* env, jclass cls) {
    return JVM_IsFinalizationEnabled(env);
}
