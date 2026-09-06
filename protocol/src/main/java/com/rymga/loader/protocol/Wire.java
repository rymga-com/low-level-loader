package com.rymga.loader.protocol;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Locale;

final class Wire {
    static final byte[] REQUEST_MAGIC = {'R', 'L', 'S', 'R'};
    static final byte[] MANIFEST_MAGIC = {'R', 'L', 'S', 'M'};

    private Wire() {
    }

    static void requireVersion(int version) {
        if (version != Protocol.VERSION) {
            throw new ProtocolException("unsupported protocol version: " + version);
        }
    }

    static void requireTimestamp(String name, long value) {
        if (value < 0) {
            throw new ProtocolException(name + " must be non-negative");
        }
    }

    static void requireBytes(String name, byte[] value, int expectedLength) {
        if (value == null || value.length != expectedLength) {
            throw new ProtocolException(name + " must contain " + expectedLength + " bytes");
        }
    }

    static void requireIdentifier(String name, String value, int maxBytes) {
        requireString(name, value, maxBytes);
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z')
                    && !(c >= '0' && c <= '9') && c != '.' && c != '_' && c != '-') {
                throw new ProtocolException(name + " must be an ASCII identifier");
            }
        }
    }

    static void requireString(String name, String value, int maxBytes) {
        if (value == null || value.isEmpty()) {
            throw new ProtocolException(name + " must not be empty");
        }
        if (value.indexOf('\u0000') >= 0) {
            throw new ProtocolException(name + " must not contain NUL");
        }
        for (int i = 0; i < value.length(); i++) {
            if (Character.isISOControl(value.charAt(i))) {
                throw new ProtocolException(name + " must not contain control characters");
            }
        }
        byte[] utf8 = utf8(name, value);
        if (utf8.length > maxBytes) {
            throw new ProtocolException(name + " exceeds " + maxBytes + " UTF-8 bytes");
        }
    }

    static void requireFilename(String filename) {
        requireString("originalFilename", filename, 255);
        String lower = filename.toLowerCase(Locale.ROOT);
        int firstDot = filename.indexOf('.');
        String base = (firstDot < 0 ? filename : filename.substring(0, firstDot)).toUpperCase(Locale.ROOT);
        boolean reserved = base.equals("CON") || base.equals("PRN") || base.equals("AUX") || base.equals("NUL")
                || base.length() == 4 && (base.startsWith("COM") || base.startsWith("LPT"))
                && base.charAt(3) >= '1' && base.charAt(3) <= '9';
        if (firstDot == 0 || reserved || filename.chars().anyMatch(character -> "<>:\"/\\|?*".indexOf(character) >= 0)
                || filename.endsWith(".") || filename.endsWith(" ") || !lower.endsWith(".jar")) {
            throw new ProtocolException("originalFilename must be a plain .jar filename");
        }
    }

    static byte[] copy(byte[] bytes) {
        return bytes == null ? null : bytes.clone();
    }

    private static byte[] utf8(String name, String value) {
        try {
            ByteBuffer encoded = StandardCharsets.UTF_8.newEncoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .encode(CharBuffer.wrap(value));
            byte[] bytes = new byte[encoded.remaining()];
            encoded.get(bytes);
            return bytes;
        } catch (CharacterCodingException exception) {
            throw new ProtocolException(name + " is not valid Unicode");
        }
    }

    static final class Writer {
        private final ByteArrayOutputStream output = new ByteArrayOutputStream();

        void magic(byte[] magic) {
            bytes(magic);
        }

        void u16(int value) {
            if (value < 0 || value > 0xffff) {
                throw new ProtocolException("u16 overflow");
            }
            output.write(value >>> 8);
            output.write(value);
        }

        void i64(long value) {
            for (int shift = 56; shift >= 0; shift -= 8) {
                output.write((int) (value >>> shift));
            }
        }

        void bytes(byte[] value) {
            output.writeBytes(value);
        }

        void string(String value) {
            byte[] utf8 = utf8("string", value);
            u16(utf8.length);
            bytes(utf8);
        }

        byte[] finish(int maxBytes) {
            byte[] encoded = output.toByteArray();
            if (encoded.length > maxBytes) {
                throw new ProtocolException("encoded message exceeds " + maxBytes + " bytes");
            }
            return encoded;
        }
    }

    static final class Reader {
        private final ByteBuffer input;

        Reader(byte[] encoded, int maxBytes) {
            if (encoded == null || encoded.length > maxBytes) {
                throw new ProtocolException("message exceeds " + maxBytes + " bytes");
            }
            input = ByteBuffer.wrap(encoded);
        }

        void magic(byte[] expected) {
            byte[] actual = bytes(expected.length);
            if (!Arrays.equals(actual, expected)) {
                throw new ProtocolException("unexpected message magic");
            }
        }

        int u16() {
            ensureRemaining(2);
            return Short.toUnsignedInt(input.getShort());
        }

        long i64() {
            ensureRemaining(8);
            return input.getLong();
        }

        byte[] bytes(int length) {
            if (length < 0) {
                throw new ProtocolException("negative field length");
            }
            ensureRemaining(length);
            byte[] value = new byte[length];
            input.get(value);
            return value;
        }

        String string(String name, int maxBytes) {
            int length = u16();
            if (length == 0 || length > maxBytes) {
                throw new ProtocolException(name + " has invalid length");
            }
            byte[] utf8 = bytes(length);
            try {
                String value = StandardCharsets.UTF_8.newDecoder()
                        .onMalformedInput(CodingErrorAction.REPORT)
                        .onUnmappableCharacter(CodingErrorAction.REPORT)
                        .decode(ByteBuffer.wrap(utf8))
                        .toString();
                if (!Arrays.equals(utf8, utf8(name, value))) {
                    throw new ProtocolException(name + " is not canonical UTF-8");
                }
                return value;
            } catch (CharacterCodingException exception) {
                throw new ProtocolException(name + " is not valid UTF-8");
            }
        }

        void finish() {
            if (input.hasRemaining()) {
                throw new ProtocolException("trailing bytes are not allowed");
            }
        }

        private void ensureRemaining(int length) {
            if (input.remaining() < length) {
                throw new ProtocolException("truncated message");
            }
        }
    }
}
