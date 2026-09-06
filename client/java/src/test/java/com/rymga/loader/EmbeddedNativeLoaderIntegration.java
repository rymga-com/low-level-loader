package com.rymga.loader;

import java.nio.file.Files;
import java.nio.file.Path;

public final class EmbeddedNativeLoaderIntegration {
    private EmbeddedNativeLoaderIntegration() {
    }

    public static void main(String[] arguments) throws Exception {
        if (arguments.length != 4) throw new IllegalArgumentException("expected JNI library, CA, runtime and license");
        NativeLoader.load(Path.of(arguments[0]));
        try (NativeArtifact artifact = NativeLoader.acquireEmbedded(Path.of(arguments[2]), Path.of(arguments[1]),
                arguments[3].toCharArray())) {
            if (!Files.isRegularFile(artifact.path())) throw new AssertionError("embedded acquisition did not produce a JAR");
        }
    }
}
