package com.rymga.loader;

import static java.nio.file.LinkOption.NOFOLLOW_LINKS;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.PosixFilePermissions;

final class ApplicationConfig {
    private static final int MAX_BYTES = 16 * 1024;
    private static final String TEMPLATE = "license:\n  user: \"\"\n  key: \"\"\n";

    private ApplicationConfig() {
    }

    static boolean create(Path file) throws IOException {
        try {
            Files.writeString(file, TEMPLATE, StandardOpenOption.CREATE_NEW);
            try {
                Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-------"));
            } catch (UnsupportedOperationException ignored) {
            }
            return true;
        } catch (FileAlreadyExistsException exception) {
            return false;
        }
    }

    static char[] read(Path file) throws IOException {
        if (!Files.isRegularFile(file, NOFOLLOW_LINKS)) {
            throw new IOException("config.yml is not a regular file of at most 16 KiB");
        }
        byte[] bytes;
        try (InputStream input = Files.newInputStream(file, StandardOpenOption.READ, NOFOLLOW_LINKS)) {
            bytes = input.readNBytes(MAX_BYTES + 1);
        }
        if (bytes.length > MAX_BYTES) throw new IOException("config.yml is not a regular file of at most 16 KiB");
        String contents;
        try {
            contents = StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString();
        } catch (CharacterCodingException exception) {
            throw invalid();
        }
        String user = null;
        String key = null;
        boolean license = false;
        for (String line : contents.lines().toList()) {
            if (line.isBlank() || line.stripLeading().startsWith("#")) continue;
            if (line.equals("license:")) {
                if (license) throw invalid();
                license = true;
                continue;
            }
            if (!license || !line.startsWith("  ") || line.length() < 4 || Character.isWhitespace(line.charAt(2))) {
                throw invalid();
            }
            int separator = line.indexOf(':', 2);
            if (separator < 0) throw invalid();
            String name = line.substring(2, separator).trim();
            String value = scalar(line.substring(separator + 1));
            if (name.equals("user") && user == null) user = value;
            else if (name.equals("key") && key == null) key = value;
            else throw invalid();
        }
        user = user == null ? "" : user.trim();
        key = key == null ? "" : key.trim();
        if (!license || key.isEmpty()) throw new IOException("license.key is missing from config.yml");
        if (user.length() + key.length() + 1 > 4096) throw new IOException("license is too long");
        if (user.isEmpty()) return key.toCharArray();
        char[] result = new char[user.length() + 1 + key.length()];
        user.getChars(0, user.length(), result, 0);
        result[user.length()] = ':';
        key.getChars(0, key.length(), result, user.length() + 1);
        return result;
    }

    private static String scalar(String input) throws IOException {
        String value = input.trim();
        if (!value.startsWith("\"")) return value;
        if (value.length() < 2 || !value.endsWith("\"")) throw invalid();
        StringBuilder result = new StringBuilder(value.length() - 2);
        for (int index = 1; index < value.length() - 1; index++) {
            char character = value.charAt(index);
            if (character == '\\') {
                if (++index == value.length() - 1) throw invalid();
                character = value.charAt(index);
                if (character != '\\' && character != '"') throw invalid();
            }
            if (Character.isISOControl(character)) throw invalid();
            result.append(character);
        }
        return result.toString();
    }

    private static IOException invalid() {
        return new IOException("config.yml must contain only license.user and license.key");
    }
}
