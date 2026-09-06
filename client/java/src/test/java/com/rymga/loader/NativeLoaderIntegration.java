package com.rymga.loader;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.Executors;

public final class NativeLoaderIntegration {
    private static final byte[] PUBLIC_KEY = {
            (byte) 0xd7, 0x5a, (byte) 0x98, 0x01, (byte) 0x82, (byte) 0xb1, 0x0a, (byte) 0xb7,
            (byte) 0xd5, 0x4b, (byte) 0xfe, (byte) 0xd3, (byte) 0xc9, 0x64, 0x07, 0x3a,
            0x0e, (byte) 0xe1, 0x72, (byte) 0xf3, (byte) 0xda, (byte) 0xa6, 0x23, 0x25,
            (byte) 0xaf, 0x02, 0x1a, 0x68, (byte) 0xf7, 0x07, 0x51, 0x1a
    };

    private NativeLoaderIntegration() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) throw new IllegalArgumentException("expected endpoint, CA bundle and runtime directory");
        Path runtime = Path.of(args[2]);
        try (var executor = Executors.newFixedThreadPool(2)) {
            var first = executor.submit(() -> { acquire(args[0], Path.of(args[1]), runtime); return null; });
            var second = executor.submit(() -> { acquire(args[0], Path.of(args[1]), runtime); return null; });
            first.get();
            second.get();
        }
    }

    private static void acquire(String endpoint, Path caBundle, Path runtime) throws Exception {
        char[] license = "dev-license".toCharArray();
        Path path;
        try (NativeArtifact artifact = NativeLoader.acquire(new NativeRequest(endpoint, "demo-plugin", "stable", "0.1.0",
                "jni-integration-test", runtime, caBundle, List.of(
                        new NativeTrustedKey("next-2026", new byte[32]), new NativeTrustedKey("dev-2026", PUBLIC_KEY))), license)) {
            for (char character : license) if (character != '\0') throw new AssertionError("license was not wiped");
            path = artifact.path();
            if (!Files.isRegularFile(artifact.path()) || Files.size(artifact.path()) < 4) {
                throw new AssertionError("native loader did not materialize the JAR");
            }
        }
        if (Files.exists(path)) throw new AssertionError("released artifact was not deleted");
    }
}
