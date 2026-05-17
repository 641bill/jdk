/*
 * Copyright (c) 2026, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "classfile/javaClasses.hpp"
#include "classfile/vmSymbols.hpp"
#include "jni.h"
#include "jvm.h"
#include "memory/allocation.inline.hpp"
#include "memory/oopFactory.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/typeArrayOop.inline.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/jniHandles.inline.hpp"
#include "runtime/riftRegionRuntime.hpp"

#define RIFT_ENTRY(result_type, header) \
  JVM_ENTRY(static result_type, header)

#define RIFT_END JVM_END

RIFT_ENTRY(jlong, RiftRegion_Open(JNIEnv* env, jclass cls, jlong capacity))
  return RiftRegionRuntime::open(thread, capacity, CHECK_0);
RIFT_END

RIFT_ENTRY(void, RiftRegion_Enter(JNIEnv* env, jclass cls, jlong handle))
  RiftRegionRuntime::enter(thread, handle, CHECK);
RIFT_END

RIFT_ENTRY(void, RiftRegion_Leave(JNIEnv* env, jclass cls, jlong handle))
  RiftRegionRuntime::leave(thread, handle, CHECK);
RIFT_END

RIFT_ENTRY(void, RiftRegion_Close(JNIEnv* env, jclass cls, jlong handle))
  RiftRegionRuntime::close(thread, handle, CHECK);
RIFT_END

RIFT_ENTRY(jlong, RiftRegion_Current(JNIEnv* env, jclass cls))
  return RiftRegionRuntime::current(thread);
RIFT_END

RIFT_ENTRY(jlong, RiftRegion_AllocateRaw(JNIEnv* env, jclass cls, jlong handle, jlong bytes))
  return RiftRegionRuntime::allocate_raw(thread, handle, bytes, CHECK_0);
RIFT_END

RIFT_ENTRY(void, RiftRegion_RegisterEligible(JNIEnv* env, jclass cls, jclass eligible_cls))
  if (eligible_cls == nullptr) {
    THROW_MSG(vmSymbols::java_lang_NullPointerException(), "eligible class is null");
  }
  Klass* klass = java_lang_Class::as_Klass(JNIHandles::resolve_non_null(eligible_cls));
  if (klass == nullptr || !klass->is_instance_klass()) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "eligible class must be an instance class");
  }
  RiftRegionRuntime::register_eligible(InstanceKlass::cast(klass), CHECK);
RIFT_END

RIFT_ENTRY(void, RiftRegion_VerifyLive(JNIEnv* env, jclass cls, jobject value))
  RiftRegionRuntime::verify_live_oop(JNIHandles::resolve(value), CHECK);
RIFT_END

RIFT_ENTRY(jlong, RiftRegion_CreateHeapRoot(JNIEnv* env, jclass cls, jobject value))
  return RiftRegionRuntime::create_heap_root(thread, JNIHandles::resolve(value), CHECK_0);
RIFT_END

RIFT_ENTRY(jobject, RiftRegion_ResolveHeapRoot(JNIEnv* env, jclass cls, jlong handle))
  oop value = RiftRegionRuntime::resolve_heap_root(handle, CHECK_NULL);
  return JNIHandles::make_local(THREAD, value);
RIFT_END

RIFT_ENTRY(void, RiftRegion_ReleaseHeapRoot(JNIEnv* env, jclass cls, jlong handle))
  RiftRegionRuntime::release_heap_root(handle, CHECK);
RIFT_END

RIFT_ENTRY(jlongArray, RiftRegion_Stats(JNIEnv* env, jclass cls, jlong handle))
  const int len = RiftRegionRuntime::StatsLength;
  jlong values[len];
  RiftRegionRuntime::stats(thread, handle, values, len, CHECK_NULL);
  typeArrayOop result = oopFactory::new_longArray(len, CHECK_NULL);
  for (int i = 0; i < len; i++) {
    result->long_at_put(i, values[i]);
  }
  return (jlongArray) JNIHandles::make_local(THREAD, result);
RIFT_END

static JNINativeMethod jdk_internal_rift_RiftRegion_methods[] = {
  { const_cast<char*>("open"),        const_cast<char*>("(J)J"),  reinterpret_cast<void*>(RiftRegion_Open) },
  { const_cast<char*>("enter"),       const_cast<char*>("(J)V"),  reinterpret_cast<void*>(RiftRegion_Enter) },
  { const_cast<char*>("leave"),       const_cast<char*>("(J)V"),  reinterpret_cast<void*>(RiftRegion_Leave) },
  { const_cast<char*>("close"),       const_cast<char*>("(J)V"),  reinterpret_cast<void*>(RiftRegion_Close) },
  { const_cast<char*>("current"),     const_cast<char*>("()J"),   reinterpret_cast<void*>(RiftRegion_Current) },
  { const_cast<char*>("allocateRaw"), const_cast<char*>("(JJ)J"), reinterpret_cast<void*>(RiftRegion_AllocateRaw) },
  { const_cast<char*>("registerEligible"), const_cast<char*>("(Ljava/lang/Class;)V"), reinterpret_cast<void*>(RiftRegion_RegisterEligible) },
  { const_cast<char*>("verifyLive"), const_cast<char*>("(Ljava/lang/Object;)V"), reinterpret_cast<void*>(RiftRegion_VerifyLive) },
  { const_cast<char*>("createHeapRoot"), const_cast<char*>("(Ljava/lang/Object;)J"), reinterpret_cast<void*>(RiftRegion_CreateHeapRoot) },
  { const_cast<char*>("resolveHeapRoot"), const_cast<char*>("(J)Ljava/lang/Object;"), reinterpret_cast<void*>(RiftRegion_ResolveHeapRoot) },
  { const_cast<char*>("releaseHeapRoot"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(RiftRegion_ReleaseHeapRoot) },
  { const_cast<char*>("stats"),       const_cast<char*>("(J)[J"), reinterpret_cast<void*>(RiftRegion_Stats) },
};

JVM_ENTRY(void, JVM_RegisterJDKInternalRiftRegionMethods(JNIEnv* env, jclass rift_region_class))
  ThreadToNativeFromVM ttnfv(thread);

  int ok = env->RegisterNatives(rift_region_class,
                                jdk_internal_rift_RiftRegion_methods,
                                sizeof(jdk_internal_rift_RiftRegion_methods) / sizeof(JNINativeMethod));
  guarantee(ok == 0, "register jdk.internal.rift.RiftRegion natives");
JVM_END
