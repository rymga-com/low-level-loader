package com.rymga.loader;

import java.io.IOException;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Comparator;

public final class ApplicationConfigSelfTest {
    private ApplicationConfigSelfTest() {
    }

    public static void main(String[] arguments) throws Exception {
        var temporary = Files.createTempDirectory("rymga-application-config-test-");
        var config = temporary.resolve("config.yml");
        try {
            require(ApplicationConfig.create(config));
            require(!ApplicationConfig.create(config));
            expectFailure(config);

            Files.writeString(config, "# credentials\nlicense:\n  user: \"user\"\n  key: \"key\\\"part\"\n");
            require(Arrays.equals("user:key\"part".toCharArray(), ApplicationConfig.read(config)));
            Files.writeString(config, "license:\n  key: plain-key\n");
            require(Arrays.equals("plain-key".toCharArray(), ApplicationConfig.read(config)));

            Files.writeString(config, "license:\n  key: one\n  key: two\n");
            expectFailure(config);
            Files.writeString(config, "license:\n  endpoint: https://wrong.example\n  key: value\n");
            expectFailure(config);
            Files.writeString(config, "x".repeat(16 * 1024 + 1));
            expectFailure(config);
        } finally {
            try (var paths = Files.walk(temporary)) {
                for (var path : paths.sorted(Comparator.reverseOrder()).toList()) Files.deleteIfExists(path);
            }
        }
        System.out.println("application config self-test: OK");
    }

    private static void expectFailure(java.nio.file.Path config) throws Exception {
        try {
            ApplicationConfig.read(config);
            throw new AssertionError();
        } catch (IOException expected) {
        }
    }

    private static void require(boolean value) {
        if (!value) throw new AssertionError();
    }
}
