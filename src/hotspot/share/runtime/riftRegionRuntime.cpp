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

#include "classfile/vmSymbols.hpp"
#include "memory/allocation.inline.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/atomic.hpp"
#include "runtime/fieldDescriptor.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/riftRegionRuntime.hpp"
#include "utilities/align.hpp"
#include "utilities/copy.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/globalDefinitions.hpp"

class RiftRegionRuntime::RegionState : public CHeapObj<mtInternal> {
 public:
  static volatile jlong _next_id;

  jlong _id;
  char* _base;
  char* _top;
  char* _end;
  size_t _capacity;
  size_t _high_water;
  jlong _opens;
  jlong _enters;
  jlong _leaves;
  jlong _closes;
  jlong _resets;
  jlong _raw_allocs;
  jlong _object_allocs;
  bool _closed;
  bool _entered;
  JavaThread* _owner;

  RegionState(jlong id, size_t capacity) :
    _id(id),
    _base(nullptr),
    _top(nullptr),
    _end(nullptr),
    _capacity(capacity),
    _high_water(0),
    _opens(1),
    _enters(0),
    _leaves(0),
    _closes(0),
    _resets(0),
    _raw_allocs(0),
    _object_allocs(0),
    _closed(false),
    _entered(false),
    _owner(nullptr) {
    _base = NEW_C_HEAP_ARRAY(char, _capacity, mtInternal);
    _top = _base;
    _end = _base + _capacity;
  }

  ~RegionState() {
    if (_base != nullptr) {
      FREE_C_HEAP_ARRAY(char, _base);
      _base = nullptr;
      _top = nullptr;
      _end = nullptr;
    }
  }

  void reset_and_free() {
    if (_base != nullptr) {
      FREE_C_HEAP_ARRAY(char, _base);
      _base = nullptr;
      _top = nullptr;
      _end = nullptr;
    }
    _resets++;
  }

  bool contains(oop obj) const {
    if (obj == nullptr || _base == nullptr) {
      return false;
    }
    char* p = cast_from_oop<char*>(obj);
    return _base <= p && p < _top;
  }
};

volatile jlong RiftRegionRuntime::RegionState::_next_id = 0;

static GrowableArray<InstanceKlass*>* rift_eligible_klasses = nullptr;
static GrowableArray<RiftRegionRuntime::RegionState*>* rift_regions = nullptr;

static RiftRegionRuntime::RegionState* state_from_handle(jlong handle) {
  return reinterpret_cast<RiftRegionRuntime::RegionState*>(static_cast<intptr_t>(handle));
}

static jlong handle_from_state(RiftRegionRuntime::RegionState* state) {
  return static_cast<jlong>(reinterpret_cast<intptr_t>(state));
}

static void require_enabled(TRAPS) {
  if (!UseRiftRegions) {
    THROW_MSG(vmSymbols::java_lang_UnsupportedOperationException(),
              "Rift regions require -XX:+UnlockExperimentalVMOptions -XX:+UseRiftRegions");
  }
}

static RiftRegionRuntime::RegionState* require_state(jlong handle, TRAPS) {
  if (handle == 0) {
    THROW_MSG_NULL(vmSymbols::java_lang_NullPointerException(), "null Rift region handle");
  }
  return state_from_handle(handle);
}

static void require_open(RiftRegionRuntime::RegionState* state, TRAPS) {
  if (state->_closed) {
    THROW_MSG(vmSymbols::java_lang_IllegalStateException(), "Rift region is closed");
  }
}

static bool is_supported_primitive_field(BasicType type) {
  switch (type) {
    case T_BOOLEAN:
    case T_BYTE:
    case T_CHAR:
    case T_SHORT:
    case T_INT:
    case T_LONG:
    case T_FLOAT:
    case T_DOUBLE:
      return true;
    default:
      return false;
  }
}

class RiftEligibilityFieldClosure : public FieldClosure {
  bool _unsupported;

 public:
  RiftEligibilityFieldClosure() : _unsupported(false) {}

  void do_field(fieldDescriptor* fd) override {
    if (!is_supported_primitive_field(fd->field_type())) {
      _unsupported = true;
    }
  }

  bool unsupported() const {
    return _unsupported;
  }
};

static void validate_eligible_klass(InstanceKlass* klass, TRAPS) {
  if (!klass->is_final()) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "Rift region object classes must be final in this prototype");
  }
  if (klass->has_finalizer()) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "Rift region object classes must not have finalizers");
  }
  if (klass->is_hidden()) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "Rift region object classes must not be hidden classes");
  }
  if (klass->is_contended()) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "Rift region object classes must not be contended classes");
  }
  RiftEligibilityFieldClosure closure;
  klass->do_nonstatic_fields(&closure);
  if (closure.unsupported()) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(),
              "Rift region object classes may only have primitive instance fields in this prototype");
  }
}

static void ensure_eligible_storage() {
  if (rift_eligible_klasses == nullptr) {
    rift_eligible_klasses = new (mtInternal) GrowableArray<InstanceKlass*>(8, mtInternal);
  }
}

static void ensure_region_storage() {
  if (rift_regions == nullptr) {
    rift_regions = new (mtInternal) GrowableArray<RiftRegionRuntime::RegionState*>(8, mtInternal);
  }
}

static HeapWord* allocate_region_words(RiftRegionRuntime::RegionState* state, size_t word_size, TRAPS) {
  size_t size = word_size * HeapWordSize;
  if (state->_base == nullptr || static_cast<size_t>(state->_end - state->_top) < size) {
    THROW_MSG_NULL(vmSymbols::java_lang_OutOfMemoryError(), "Rift region arena exhausted");
  }
  char* result = state->_top;
  state->_top += size;
  size_t used = static_cast<size_t>(state->_top - state->_base);
  if (used > state->_high_water) {
    state->_high_water = used;
  }
  return reinterpret_cast<HeapWord*>(result);
}

bool RiftRegionRuntime::has_current_region(JavaThread* current) {
  return current->rift_current_region() != nullptr;
}

bool RiftRegionRuntime::is_region_oop(oop obj) {
  if (obj == nullptr || rift_regions == nullptr) {
    return false;
  }
  for (int i = 0; i < rift_regions->length(); i++) {
    RegionState* state = rift_regions->at(i);
    if (!state->_closed && state->contains(obj)) {
      return true;
    }
  }
  return false;
}

static bool is_region_address(address dst) {
  if (dst == nullptr || rift_regions == nullptr) {
    return false;
  }
  char* p = reinterpret_cast<char*>(dst);
  for (int i = 0; i < rift_regions->length(); i++) {
    RiftRegionRuntime::RegionState* state = rift_regions->at(i);
    if (!state->_closed && state->_base != nullptr && state->_base <= p && p < state->_top) {
      return true;
    }
  }
  return false;
}

jlong RiftRegionRuntime::open(JavaThread* current, jlong capacity, TRAPS) {
  require_enabled(CHECK_0);
  if (capacity <= 0) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "capacity must be positive");
  }
  if (current->rift_current_region() != nullptr) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalStateException(), "nested Rift regions are not supported in this prototype");
  }
  RegionState* state = new RegionState(Atomic::add(&RegionState::_next_id, 1), static_cast<size_t>(capacity));
  ensure_region_storage();
  rift_regions->append(state);
  return handle_from_state(state);
}

void RiftRegionRuntime::enter(JavaThread* current, jlong handle, TRAPS) {
  require_enabled(CHECK);
  RegionState* state = require_state(handle, CHECK);
  require_open(state, CHECK);
  if (current->rift_current_region() != nullptr) {
    THROW_MSG(vmSymbols::java_lang_IllegalStateException(), "thread already has an active Rift region");
  }
  state->_entered = true;
  state->_owner = current;
  state->_enters++;
  current->set_rift_current_region(state);
}

void RiftRegionRuntime::leave(JavaThread* current, jlong handle, TRAPS) {
  require_enabled(CHECK);
  RegionState* state = require_state(handle, CHECK);
  require_open(state, CHECK);
  if (current->rift_current_region() != state) {
    THROW_MSG(vmSymbols::java_lang_IllegalStateException(), "Rift region is not active on this thread");
  }
  state->_entered = false;
  state->_leaves++;
  current->set_rift_current_region(nullptr);
}

void RiftRegionRuntime::close(JavaThread* current, jlong handle, TRAPS) {
  require_enabled(CHECK);
  RegionState* state = require_state(handle, CHECK);
  require_open(state, CHECK);
  if (current->rift_current_region() == state) {
    current->set_rift_current_region(nullptr);
    state->_entered = false;
    state->_leaves++;
  } else if (state->_entered) {
    THROW_MSG(vmSymbols::java_lang_IllegalStateException(), "Rift region is active on another thread");
  }
  state->_closed = true;
  state->_closes++;
  state->reset_and_free();
}

jlong RiftRegionRuntime::current(JavaThread* current) {
  return handle_from_state(reinterpret_cast<RegionState*>(current->rift_current_region()));
}

jlong RiftRegionRuntime::allocate_raw(JavaThread* current, jlong handle, jlong bytes, TRAPS) {
  require_enabled(CHECK_0);
  RegionState* state = require_state(handle, CHECK_0);
  require_open(state, CHECK_0);
  if (current->rift_current_region() != state) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalStateException(), "Rift region is not active on this thread");
  }
  if (bytes <= 0) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "allocation size must be positive");
  }
  size_t size = align_up(static_cast<size_t>(bytes), HeapWordSize);
  size_t word_size = size / HeapWordSize;
  if (word_size == 0) {
    THROW_MSG_0(vmSymbols::java_lang_OutOfMemoryError(), "Rift raw arena exhausted");
  }
  HeapWord* result = allocate_region_words(state, word_size, CHECK_0);
  state->_raw_allocs++;
  return static_cast<jlong>(reinterpret_cast<intptr_t>(result));
}

void RiftRegionRuntime::register_eligible(InstanceKlass* klass, TRAPS) {
  require_enabled(CHECK);
  validate_eligible_klass(klass, CHECK);
  ensure_eligible_storage();
  if (!is_eligible(klass)) {
    rift_eligible_klasses->append(klass);
  }
}

bool RiftRegionRuntime::is_eligible(InstanceKlass* klass) {
  if (rift_eligible_klasses == nullptr) {
    return false;
  }
  for (int i = 0; i < rift_eligible_klasses->length(); i++) {
    if (rift_eligible_klasses->at(i) == klass) {
      return true;
    }
  }
  return false;
}

oop RiftRegionRuntime::allocate_instance(JavaThread* current, InstanceKlass* klass, TRAPS) {
  require_enabled(CHECK_NULL);
  RegionState* state = reinterpret_cast<RegionState*>(current->rift_current_region());
  if (state == nullptr) {
    return nullptr;
  }
  require_open(state, CHECK_NULL);
  if (!is_eligible(klass)) {
    return nullptr;
  }
  if (UseCompressedOops) {
    THROW_MSG_NULL(vmSymbols::java_lang_UnsupportedOperationException(),
                   "Rift region object allocation prototype requires -XX:-UseCompressedOops");
  }
  if (UseCompactObjectHeaders) {
    THROW_MSG_NULL(vmSymbols::java_lang_UnsupportedOperationException(),
                   "Rift region object allocation prototype requires -XX:-UseCompactObjectHeaders");
  }

  validate_eligible_klass(klass, CHECK_NULL);
  size_t word_size = klass->size_helper();
  HeapWord* mem = allocate_region_words(state, word_size, CHECK_NULL);

  const size_t header_size = oopDesc::header_size();
  assert(word_size >= header_size, "unexpected object size");
  if (oopDesc::has_klass_gap()) {
    oopDesc::set_klass_gap(mem, 0);
  }
  Copy::fill_to_aligned_words(mem + header_size, word_size - header_size);
  oopDesc::set_mark(mem, markWord::prototype());
  oopDesc::release_set_klass(mem, klass);

  state->_object_allocs++;
  return cast_to_oop(mem);
}

void RiftRegionRuntime::verify_oop_store(oop value, address dst, TRAPS) {
  if (value == nullptr || !UseRiftRegions) {
    return;
  }
  if (!is_region_oop(value)) {
    return;
  }
  if (is_region_address(dst)) {
    return;
  }
  THROW_MSG(vmSymbols::java_lang_IllegalStateException(),
            "heap object retains an object allocated in a Rift region");
}

void RiftRegionRuntime::stats(JavaThread* current, jlong handle, jlong* out, int len, TRAPS) {
  require_enabled(CHECK);
  RegionState* state = require_state(handle, CHECK);
  if (len < StatsLength) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "stats array is too small");
  }
  size_t used = state->_base == nullptr ? 0 : static_cast<size_t>(state->_top - state->_base);
  out[0] = state->_id;
  out[1] = state->_closed ? 0 : 1;
  out[2] = state->_entered ? 1 : 0;
  out[3] = static_cast<jlong>(state->_capacity);
  out[4] = static_cast<jlong>(used);
  out[5] = static_cast<jlong>(state->_high_water);
  out[6] = state->_opens;
  out[7] = state->_enters;
  out[8] = state->_leaves;
  out[9] = state->_closes;
  out[10] = state->_resets;
  out[11] = state->_raw_allocs;
  out[12] = state->_object_allocs;
}
