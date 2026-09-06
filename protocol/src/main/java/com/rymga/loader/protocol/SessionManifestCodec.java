package com.rymga.loader.protocol;

final class SessionManifestCodec {
    private SessionManifestCodec() {
    }

    static byte[] encodeUnsigned(SessionManifest manifest) {
        Wire.Writer writer = new Wire.Writer();
        writer.magic(Wire.MANIFEST_MAGIC);
        writer.u16(Protocol.VERSION);
        writer.string(manifest.keyId());
        writer.bytes(manifest.requestNonce());
        writer.bytes(manifest.sessionId());
        writer.i64(manifest.issuedAt());
        writer.i64(manifest.expiresAt());
        writer.string(manifest.productId());
        writer.string(manifest.channel());
        writer.string(manifest.artifactId());
        writer.string(manifest.originalFilename());
        writer.i64(manifest.artifactSize());
        writer.bytes(manifest.artifactSha256());
        return writer.finish(Protocol.MAX_SESSION_MANIFEST_BYTES - Protocol.ED25519_SIGNATURE_BYTES);
    }

    static byte[] encode(SessionManifest manifest) {
        Wire.Writer writer = new Wire.Writer();
        writer.bytes(encodeUnsigned(manifest));
        writer.bytes(manifest.signature());
        return writer.finish(Protocol.MAX_SESSION_MANIFEST_BYTES);
    }

    static SessionManifest decode(byte[] encoded) {
        Wire.Reader reader = new Wire.Reader(encoded, Protocol.MAX_SESSION_MANIFEST_BYTES);
        reader.magic(Wire.MANIFEST_MAGIC);
        Wire.requireVersion(reader.u16());
        SessionManifest manifest = SessionManifest.decoded(
                reader.string("keyId", 64),
                reader.bytes(Protocol.NONCE_BYTES),
                reader.bytes(Protocol.SESSION_ID_BYTES),
                reader.i64(),
                reader.i64(),
                reader.string("productId", 128),
                reader.string("channel", 64),
                reader.string("artifactId", 128),
                reader.string("originalFilename", 255),
                reader.i64(),
                reader.bytes(Protocol.SHA256_BYTES),
                reader.bytes(Protocol.ED25519_SIGNATURE_BYTES));
        reader.finish();
        return manifest;
    }
}
