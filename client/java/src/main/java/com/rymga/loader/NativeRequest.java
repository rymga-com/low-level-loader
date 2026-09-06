package com.rymga.loader;

import java.nio.file.Path;
import java.util.List;
import java.util.Objects;

public record NativeRequest(String endpoint, String productId, String channel, String loaderVersion,
                            String installationId, Path runtimeDirectory, Path caBundle,
                            List<NativeTrustedKey> trustedKeys) {
    public NativeRequest {
        Objects.requireNonNull(endpoint);
        Objects.requireNonNull(productId);
        Objects.requireNonNull(channel);
        Objects.requireNonNull(loaderVersion);
        Objects.requireNonNull(installationId);
        Objects.requireNonNull(runtimeDirectory);
        Objects.requireNonNull(trustedKeys);
        trustedKeys = List.copyOf(trustedKeys);
        if (trustedKeys.isEmpty() || trustedKeys.size() > 16) throw new IllegalArgumentException("trustedKeys must contain 1..16 keys");
        if (trustedKeys.stream().map(NativeTrustedKey::keyId).distinct().count() != trustedKeys.size())
            throw new IllegalArgumentException("trusted key IDs must be unique");
    }

    public NativeRequest(String endpoint, String productId, String channel, String loaderVersion,
                         String installationId, Path runtimeDirectory, Path caBundle,
                         String keyId, byte[] publicKey) {
        this(endpoint, productId, channel, loaderVersion, installationId, runtimeDirectory, caBundle,
                List.of(new NativeTrustedKey(keyId, publicKey)));
    }
}
