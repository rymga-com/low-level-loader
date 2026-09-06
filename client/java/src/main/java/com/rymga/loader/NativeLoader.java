package com.rymga.loader;

import java.io.IOException;
import java.nio.file.Path;
import java.util.Objects;

public final class NativeLoader {
    private static boolean loaded;

    private NativeLoader() {
    }

    public static NativeArtifact acquire(NativeRequest request, char[] license) throws IOException {
        Objects.requireNonNull(request);
        Objects.requireNonNull(license);
        ensureLoaded();
        try {
            String[] keyIds = request.trustedKeys().stream().map(NativeTrustedKey::keyId).toArray(String[]::new);
            byte[][] publicKeys = request.trustedKeys().stream().map(NativeTrustedKey::publicKey).toArray(byte[][]::new);
            long handle = acquire0(request.endpoint(), request.productId(), request.channel(), request.loaderVersion(),
                    request.installationId(), request.runtimeDirectory().toString(),
                    request.caBundle() == null ? null : request.caBundle().toString(), keyIds, publicKeys, license);
            return artifact(handle);
        } finally {
            java.util.Arrays.fill(license, '\0');
        }
    }

    public static NativeArtifact acquireEmbedded(Path runtimeDirectory, Path caBundle, char[] license) throws IOException {
        Objects.requireNonNull(runtimeDirectory);
        Objects.requireNonNull(license);
        ensureLoaded();
        try {
            return artifact(acquireEmbedded0(runtimeDirectory.toString(), caBundle == null ? null : caBundle.toString(), license));
        } finally {
            java.util.Arrays.fill(license, '\0');
        }
    }

    private static NativeArtifact artifact(long handle) {
        try {
            return new NativeArtifact(handle, Path.of(path0(handle)));
        } catch (RuntimeException | Error exception) {
            release0(handle);
            throw exception;
        }
    }

    public static synchronized void load(Path library) {
        Objects.requireNonNull(library);
        if (!loaded) {
            System.load(library.toAbsolutePath().normalize().toString());
            loaded = true;
        }
    }

    private static synchronized void ensureLoaded() {
        if (!loaded) {
            System.loadLibrary("rymga_loader_jni");
            loaded = true;
        }
    }

    private static native long acquire0(String endpoint, String productId, String channel, String loaderVersion,
                                        String installationId, String runtimeDirectory, String caBundle,
                                        String[] keyIds, byte[][] publicKeys, char[] license) throws IOException;
    private static native long acquireEmbedded0(String runtimeDirectory, String caBundle, char[] license) throws IOException;
    static native void release0(long handle);
    private static native String path0(long handle);
}
