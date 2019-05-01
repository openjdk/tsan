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

#include <jni.h>
#include <jvmti.h>
#include <pthread.h>
#include <string.h>

extern "C" {

static int global;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static jvmtiEnv *jvmti = NULL;

JNIEXPORT void JNICALL Java_AbstractNativeLoop_writeNativeGlobalSync(
    JNIEnv *env, jclass unused) {
  pthread_mutex_lock(&mutex);
  global = 123;
  pthread_mutex_unlock(&mutex);
}

JNIEXPORT void JNICALL Java_AbstractNativeLoop_writeNativeGlobal(
    JNIEnv *env, jclass unused) {
  global = 123;
}

JNIEXPORT jint JNICALL Java_AbstractNativeLoop_readNativeGlobal(JNIEnv *env,
                                                                jclass unused) {
  return global;
}

JNIEXPORT jlong JNICALL Java_AbstractNativeLoop_createRawLock(JNIEnv *env,
                                                              jclass unused) {
  jrawMonitorID lock;
  jvmti->CreateRawMonitor("lock", &lock);
  return reinterpret_cast<jlong>(lock);
}

JNIEXPORT void JNICALL Java_AbstractNativeLoop_writeRawLockedNativeGlobal(
    JNIEnv *env, jclass unused, long lock) {
  jrawMonitorID raw_lock = reinterpret_cast<jrawMonitorID>(lock);
  jvmti->RawMonitorEnter(raw_lock);
  global = 123;
  jvmti->RawMonitorExit(raw_lock);
}

JNIEXPORT jboolean JNICALL Java_JvmtiTaggerLoopRunner_addTagAndReference(
    JNIEnv *env, jclass unused, jobject object) {
  // Create a global reference so that GC won't take this object.
  env->NewGlobalRef(object);

  // Create a pointer for the tag.
  int *ptr = new int;
  *ptr = 42;
  return jvmti->SetTag(object, reinterpret_cast<long>(ptr))
      == JVMTI_ERROR_NONE;
}

static JNICALL jint PerObjectCallback(jlong class_tag, jlong size,
                                      jlong *tag_ptr, jint length,
                                      void *user_data) {
  int *sum_ptr = reinterpret_cast<int*>(user_data);
  int *ptr = reinterpret_cast<int*>(*tag_ptr);

  // We don't use an atomic since it is not yet supported.
  pthread_mutex_lock(&mutex);
  *sum_ptr += *ptr;
  pthread_mutex_unlock(&mutex);
  return JVMTI_VISIT_OBJECTS;
}

JNIEXPORT jboolean JNICALL Java_JvmtiTaggerLoopRunner_iterateOverTags(
    JNIEnv *env, jclass unused) {
  jvmtiHeapCallbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.heap_iteration_callback = &PerObjectCallback;

  // TODO: we really do not need this mutex normally but TSAN is not happy with
  // the way things happen via the JVMTI call. We will need to support this at
  // some point (it comes from the VM Operation system...)
  // We don't use an atomic since it is not yet supported.
  pthread_mutex_lock(&mutex);
  int sum = 0;
  pthread_mutex_unlock(&mutex);

  if (jvmti->IterateThroughHeap(JVMTI_HEAP_FILTER_UNTAGGED, NULL, &callbacks, &sum)
      != JVMTI_ERROR_NONE) {
    return false;
  }

  // We don't use an atomic since it is not yet supported.
  pthread_mutex_lock(&mutex);
  int local_value = sum;
  pthread_mutex_unlock(&mutex);
  return local_value != 0;
}

static
jint Agent_Initialize(JavaVM *jvm, char *options, void *reserved) {
  if (jvm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION) != JNI_OK) {
    return JNI_ERR;
  }

  jvmtiCapabilities caps;
  memset(&caps, 0, sizeof(caps));
  caps.can_tag_objects = 1;

  if (jvmti->AddCapabilities(&caps) != JVMTI_ERROR_NONE) {
    return JNI_ERR;
  }

  return JNI_OK;
}

JNIEXPORT
jint JNICALL Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {
  return Agent_Initialize(jvm, options, reserved);
}

JNIEXPORT
jint JNICALL Agent_OnAttach(JavaVM *jvm, char *options, void *reserved) {
  return Agent_Initialize(jvm, options, reserved);
}

JNIEXPORT
jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved) {
  return JNI_VERSION_1_8;
}

}
