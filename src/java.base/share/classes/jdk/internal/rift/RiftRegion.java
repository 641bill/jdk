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

package jdk.internal.rift;

/**
 * Internal VM test surface for the experimental Rift region backend.
 *
 * This is not a public Java or Scala API. Source-level Rift APIs should lower
 * to VM entrypoints only after capture/separation checks prove the lifetime
 * topology.
 */
public final class RiftRegion {
    private RiftRegion() {}

    static {
        registerNatives();
    }

    private static native void registerNatives();

    public static native long open(long capacity);
    public static native void enter(long handle);
    public static native void leave(long handle);
    public static native void close(long handle);
    public static native long current();
    public static native long allocateRaw(long handle, long bytes);
    public static native void registerEligible(Class<?> cls);
    public static native void verifyLive(Object value);
    public static native long[] stats(long handle);

    public static void epoch(long capacity, Runnable body) {
        long handle = open(capacity);
        enter(handle);
        try {
            body.run();
        } finally {
            close(handle);
        }
    }
}
