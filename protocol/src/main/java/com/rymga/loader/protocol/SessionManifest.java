package com.rymga.loader.protocol;

import java.security.PrivateKey;
import java.security.PublicKey;

public final class SessionManifest {
    private final String keyId;
    private final byte[] requestNonce;
    private final byte[] sessionId;
    private final long issuedAt;
    private final long expiresAt;
    private final String productId;
    private final String channel;
    private final String artifactId;
    private final String originalFilename;
    private final long artifactSize;
    private final byte[] artifactSha256;
    private final byte[] signature;

    private SessionManifest(String keyId, byte[] requestNonce, byte[] sessionId, long issuedAt, long expiresAt,
                            String productId, String channel, String artifactId, String originalFilename,
                            long artifactSize, byte[] artifactSha256, byte[] signature) {
        Wire.requireIdentifier("keyId", keyId, 64);
        Wire.requireBytes("requestNonce", requestNonce, Protocol.NONCE_BYTES);
        Wire.requireBytes("sessionId", sessionId, Protocol.SESSION_ID_BYTES);
        Wire.requireTimestamp("issuedAt", issuedAt);
        Wire.requireTimestamp("expiresAt", expiresAt);
        if (expiresAt <= issuedAt) {
            throw new ProtocolException("expiresAt must be after issuedAt");
        }
        Wire.requireIdentifier("productId", productId, 128);
        Wire.requireIdentifier("channel", channel, 64);
        Wire.requireIdentifier("artifactId", artifactId, 128);
        Wire.requireFilename(originalFilename);
        if (artifactSize < 0 || artifactSize > Protocol.MAX_ARTIFACT_BYTES) {
            throw new ProtocolException("artifactSize is outside the protocol limit");
        }
        Wire.requireBytes("artifactSha256", artifactSha256, Protocol.SHA256_BYTES);
        Wire.requireBytes("signature", signature, Protocol.ED25519_SIGNATURE_BYTES);

        this.keyId = keyId;
        this.requestNonce = Wire.copy(requestNonce);
        this.sessionId = Wire.copy(sessionId);
        this.issuedAt = issuedAt;
        this.expiresAt = expiresAt;
        this.productId = productId;
        this.channel = channel;
        this.artifactId = artifactId;
        this.originalFilename = originalFilename;
        this.artifactSize = artifactSize;
        this.artifactSha256 = Wire.copy(artifactSha256);
        this.signature = Wire.copy(signature);
    }

    public static SessionManifest unsigned(String keyId, byte[] requestNonce, byte[] sessionId,
                                           long issuedAt, long expiresAt, String productId, String channel,
                                           String artifactId, String originalFilename, long artifactSize,
                                           byte[] artifactSha256) {
        return new SessionManifest(keyId, requestNonce, sessionId, issuedAt, expiresAt, productId, channel,
                artifactId, originalFilename, artifactSize, artifactSha256,
                new byte[Protocol.ED25519_SIGNATURE_BYTES]);
    }

    public String keyId() {
        return keyId;
    }

    public byte[] requestNonce() {
        return Wire.copy(requestNonce);
    }

    public byte[] sessionId() {
        return Wire.copy(sessionId);
    }

    public long issuedAt() {
        return issuedAt;
    }

    public long expiresAt() {
        return expiresAt;
    }

    public String productId() {
        return productId;
    }

    public String channel() {
        return channel;
    }

    public String artifactId() {
        return artifactId;
    }

    public String originalFilename() {
        return originalFilename;
    }

    public long artifactSize() {
        return artifactSize;
    }

    public byte[] artifactSha256() {
        return Wire.copy(artifactSha256);
    }

    public byte[] signature() {
        return Wire.copy(signature);
    }

    public byte[] unsignedBytes() {
        return SessionManifestCodec.encodeUnsigned(this);
    }

    public byte[] encode() {
        return SessionManifestCodec.encode(this);
    }

    public SessionManifest sign(PrivateKey privateKey) {
        byte[] signed = Ed25519.sign(privateKey, unsignedBytes());
        return new SessionManifest(keyId, requestNonce, sessionId, issuedAt, expiresAt, productId, channel,
                artifactId, originalFilename, artifactSize, artifactSha256, signed);
    }

    public boolean verify(PublicKey publicKey) {
        return Ed25519.verify(publicKey, unsignedBytes(), signature);
    }

    public static SessionManifest decode(byte[] encoded) {
        return SessionManifestCodec.decode(encoded);
    }

    static SessionManifest decoded(String keyId, byte[] requestNonce, byte[] sessionId, long issuedAt,
                                   long expiresAt, String productId, String channel, String artifactId,
                                   String originalFilename, long artifactSize, byte[] artifactSha256,
                                   byte[] signature) {
        return new SessionManifest(keyId, requestNonce, sessionId, issuedAt, expiresAt, productId, channel,
                artifactId, originalFilename, artifactSize, artifactSha256, signature);
    }
}
