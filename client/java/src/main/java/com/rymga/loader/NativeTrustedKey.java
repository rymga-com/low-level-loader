package com.rymga.loader;

import java.util.Arrays;
import java.util.Objects;

public record NativeTrustedKey(String keyId, byte[] publicKey) {
    public NativeTrustedKey {
        Objects.requireNonNull(keyId);
        Objects.requireNonNull(publicKey);
        if (keyId.isEmpty() || keyId.length() > 64 || !keyId.chars().allMatch(character ->
                character >= 'a' && character <= 'z' || character >= 'A' && character <= 'Z' ||
                        character >= '0' && character <= '9' || character == '.' || character == '_' || character == '-'))
            throw new IllegalArgumentException("keyId must be an ASCII identifier of at most 64 bytes");
        publicKey = publicKey.clone();
        if (publicKey.length != 32) throw new IllegalArgumentException("publicKey must contain 32 bytes");
    }

    @Override
    public byte[] publicKey() {
        return Arrays.copyOf(publicKey, publicKey.length);
    }
}
