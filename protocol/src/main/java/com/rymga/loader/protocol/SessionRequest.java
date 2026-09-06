package com.rymga.loader.protocol;

public final class SessionRequest {
    private final long timestamp;
    private final byte[] requestNonce;
    private final String productId;
    private final String channel;
    private final String loaderVersion;
    private final String installationId;
    private final String license;

    public SessionRequest(long timestamp, byte[] requestNonce, String productId, String channel,
                          String loaderVersion, String installationId, String license) {
        Wire.requireTimestamp("timestamp", timestamp);
        Wire.requireBytes("requestNonce", requestNonce, Protocol.NONCE_BYTES);
        Wire.requireIdentifier("productId", productId, 128);
        Wire.requireIdentifier("channel", channel, 64);
        Wire.requireString("loaderVersion", loaderVersion, 64);
        Wire.requireString("installationId", installationId, 128);
        Wire.requireString("license", license, 4096);

        this.timestamp = timestamp;
        this.requestNonce = Wire.copy(requestNonce);
        this.productId = productId;
        this.channel = channel;
        this.loaderVersion = loaderVersion;
        this.installationId = installationId;
        this.license = license;
    }

    public long timestamp() {
        return timestamp;
    }

    public byte[] requestNonce() {
        return Wire.copy(requestNonce);
    }

    public String productId() {
        return productId;
    }

    public String channel() {
        return channel;
    }

    public String loaderVersion() {
        return loaderVersion;
    }

    public String installationId() {
        return installationId;
    }

    public String license() {
        return license;
    }

    public byte[] encode() {
        return SessionRequestCodec.encode(this);
    }

    public static SessionRequest decode(byte[] encoded) {
        return SessionRequestCodec.decode(encoded);
    }
}
