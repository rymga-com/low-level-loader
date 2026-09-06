package com.rymga.loader;

import static java.nio.file.LinkOption.NOFOLLOW_LINKS;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HexFormat;
import java.util.Locale;

public final class BundledNativeLibrary {
    private static final int MAX_LIBRARY_BYTES = 64 * 1024 * 1024;

    private BundledNativeLibrary() {
    }

    public static Path extract(Path dataDirectory) throws IOException {
        privateDirectory(dataDirectory);
        String platform = platform(System.getProperty("os.name"), System.getProperty("os.arch"));
        String filename = filename(platform);
        String resource = "/META-INF/rymga/native/" + platform + "/" + filename;
        try (InputStream input = BundledNativeLibrary.class.getResourceAsStream(resource)) {
            if (input == null) throw new IOException("native library is not bundled for " + platform);
            byte[] bytes = input.readNBytes(MAX_LIBRARY_BYTES + 1);
            if (bytes.length == 0 || bytes.length > MAX_LIBRARY_BYTES) {
                throw new IOException("bundled native library has an invalid size");
            }
            return install(dataDirectory.resolve("native"), filename, bytes);
        }
    }

    static String platform(String osName, String architecture) throws IOException {
        String os = osName.toLowerCase(Locale.ROOT);
        String arch = architecture.toLowerCase(Locale.ROOT);
        String normalizedOs = os.contains("mac") || os.contains("darwin") ? "macos"
                : os.contains("win") ? "windows" : os.contains("linux") ? "linux" : null;
        String normalizedArch = arch.equals("amd64") || arch.equals("x86_64") ? "x86_64"
                : arch.equals("aarch64") || arch.equals("arm64") ? "aarch64" : null;
        if (normalizedOs == null || normalizedArch == null) {
            throw new IOException("unsupported native platform: " + osName + "/" + architecture);
        }
        return normalizedOs + "-" + normalizedArch;
    }

    static Path install(Path root, String filename, byte[] bytes) throws IOException {
        byte[] expected = sha256(bytes);
        Path directory = root.resolve(HexFormat.of().formatHex(expected));
        privateDirectory(root);
        privateDirectory(directory);
        Path target = directory.resolve(filename);
        if (matches(target, expected)) return target;
        Files.deleteIfExists(target);

        Path partial = Files.createTempFile(directory, ".rymga-native-", ".part");
        try {
            privateFile(partial);
            Files.write(partial, bytes, StandardOpenOption.TRUNCATE_EXISTING);
            try {
                Files.move(partial, target, StandardCopyOption.ATOMIC_MOVE);
            } catch (AtomicMoveNotSupportedException exception) {
                Files.move(partial, target);
            } catch (FileAlreadyExistsException exception) {
                if (!matches(target, expected)) throw exception;
            }
            privateFile(target);
            if (!matches(target, expected)) throw new IOException("extracted native library failed verification");
            return target;
        } finally {
            Files.deleteIfExists(partial);
        }
    }

    private static String filename(String platform) {
        if (platform.startsWith("windows-")) return "rymga_loader_jni.dll";
        if (platform.startsWith("macos-")) return "librymga_loader_jni.dylib";
        return "librymga_loader_jni.so";
    }

    private static void privateDirectory(Path directory) throws IOException {
        try {
            Files.createDirectory(directory);
        } catch (FileAlreadyExistsException exception) {
            if (!Files.isDirectory(directory, NOFOLLOW_LINKS)) throw new IOException("native path is not a directory", exception);
        }
        try {
            Files.setPosixFilePermissions(directory, PosixFilePermissions.fromString("rwx------"));
        } catch (UnsupportedOperationException ignored) {
        }
    }

    private static void privateFile(Path file) throws IOException {
        try {
            Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-------"));
        } catch (UnsupportedOperationException ignored) {
        }
    }

    private static boolean matches(Path file, byte[] expected) throws IOException {
        return Files.isRegularFile(file, NOFOLLOW_LINKS) && MessageDigest.isEqual(expected, sha256(file));
    }

    private static byte[] sha256(byte[] bytes) {
        return digest().digest(bytes);
    }

    private static byte[] sha256(Path file) throws IOException {
        MessageDigest digest = digest();
        try (InputStream input = Files.newInputStream(file)) {
            byte[] buffer = new byte[16 * 1024];
            for (int read; (read = input.read(buffer)) >= 0;) if (read != 0) digest.update(buffer, 0, read);
        }
        return digest.digest();
    }

    private static MessageDigest digest() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException exception) {
            throw new IllegalStateException("SHA-256 is unavailable", exception);
        }
    }
}
