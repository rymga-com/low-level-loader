package com.rymga.loader;

import java.nio.file.Path;

public final class NativeArtifact implements AutoCloseable {
    private long handle;
    private final Path path;

    NativeArtifact(long handle, Path path) {
        this.handle = handle;
        this.path = path;
    }

    public Path path() {
        return path;
    }

    @Override
    public synchronized void close() {
        if (handle != 0) {
            NativeLoader.release0(handle);
            handle = 0;
        }
    }
}
