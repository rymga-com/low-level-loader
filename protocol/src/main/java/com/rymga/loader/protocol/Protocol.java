package com.rymga.loader.protocol;

public final class Protocol {
    public static final int VERSION = 1;
    public static final int NONCE_BYTES = 32;
    public static final int SESSION_ID_BYTES = 32;
    public static final int SHA256_BYTES = 32;
    public static final int ED25519_SIGNATURE_BYTES = 64;

    public static final int MAX_SESSION_REQUEST_BYTES = 8 * 1024;
    public static final int MAX_SESSION_MANIFEST_BYTES = 4 * 1024;
    public static final long MAX_ARTIFACT_BYTES = 2L * 1024 * 1024 * 1024;

    private Protocol() {
    }
}
