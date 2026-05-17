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

#ifndef SHARE_RUNTIME_RIFTREGIONRUNTIME_HPP
#define SHARE_RUNTIME_RIFTREGIONRUNTIME_HPP

#include "memory/allocation.hpp"
#include "utilities/globalDefinitions.hpp"

class JavaThread;
class InstanceKlass;

class RiftRegionRuntime : public AllStatic {
 public:
  static const int StatsLength = 13;

  class RegionState;

  static bool has_current_region(JavaThread* current);
  static bool is_region_oop(oop obj);
  static jlong open(JavaThread* current, jlong capacity, TRAPS);
  static void enter(JavaThread* current, jlong handle, TRAPS);
  static void leave(JavaThread* current, jlong handle, TRAPS);
  static void close(JavaThread* current, jlong handle, TRAPS);
  static jlong current(JavaThread* current);
  static jlong allocate_raw(JavaThread* current, jlong handle, jlong bytes, TRAPS);
  static void register_eligible(InstanceKlass* klass, TRAPS);
  static bool is_eligible(InstanceKlass* klass);
  static oop allocate_instance(JavaThread* current, InstanceKlass* klass, TRAPS);
  static void verify_live_oop(oop value, TRAPS);
  static void verify_oop_store(oop value, address dst, TRAPS);
  static void verify_oop_store_to_base(oop value, oop base, TRAPS);
  static jlong create_heap_root(JavaThread* current, oop value, TRAPS);
  static oop resolve_heap_root(jlong handle, TRAPS);
  static void release_heap_root(jlong handle, TRAPS);
  static void stats(JavaThread* current, jlong handle, jlong* out, int len, TRAPS);
};

#endif // SHARE_RUNTIME_RIFTREGIONRUNTIME_HPP
