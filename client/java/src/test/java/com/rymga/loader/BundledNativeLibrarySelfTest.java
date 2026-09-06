package com.rymga.loader;

import java.nio.file.Files;
import java.util.Arrays;
import java.util.Comparator;

public final class BundledNativeLibrarySelfTest {
    private BundledNativeLibrarySelfTest() {
    }

    public static void main(String[] arguments) throws Exception {
        if (arguments.length == 1) {
            var parent = Files.createDirectories(java.nio.file.Path.of(arguments[0]));
            var dataDirectory = parent.resolve("fresh-" + System.nanoTime());
            var bundled = BundledNativeLibrary.extract(dataDirectory);
            require(Files.isRegularFile(bundled));
            require(Files.size(bundled) > 0);
            NativeLoader.load(bundled);
        }
        require(BundledNativeLibrary.platform("Linux", "amd64").equals("linux-x86_64"));
        require(BundledNativeLibrary.platform("Windows 11", "aarch64").equals("windows-aarch64"));
        require(BundledNativeLibrary.platform("Mac OS X", "arm64").equals("macos-aarch64"));

        var temporary = Files.createTempDirectory("rymga-bundled-native-test-");
        try {
            byte[] expected = {1, 2, 3, 4};
            var dataDirectory = temporary.resolve("new-data-directory");
            var root = Files.createDirectories(dataDirectory).resolve("native");
            var first = BundledNativeLibrary.install(root, "library.bin", expected);
            var second = BundledNativeLibrary.install(root, "library.bin", expected);
            require(first.equals(second));
            require(Arrays.equals(expected, Files.readAllBytes(second)));
            Files.write(second, new byte[]{9});
            require(Arrays.equals(expected, Files.readAllBytes(
                    BundledNativeLibrary.install(root, "library.bin", expected))));
        } finally {
            try (var paths = Files.walk(temporary)) {
                for (var path : paths.sorted(Comparator.reverseOrder()).toList()) Files.deleteIfExists(path);
            }
        }
        System.out.println("bundled native library self-test: OK");
    }

    private static void require(boolean value) {
        if (!value) throw new AssertionError();
    }
}
