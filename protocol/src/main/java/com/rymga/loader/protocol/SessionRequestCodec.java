package com.rymga.loader.protocol;

final class SessionRequestCodec {
    private SessionRequestCodec() {
    }

    static byte[] encode(SessionRequest request) {
        Wire.Writer writer = new Wire.Writer();
        writer.magic(Wire.REQUEST_MAGIC);
        writer.u16(Protocol.VERSION);
        writer.i64(request.timestamp());
        writer.bytes(request.requestNonce());
        writer.string(request.productId());
        writer.string(request.channel());
        writer.string(request.loaderVersion());
        writer.string(request.installationId());
        writer.string(request.license());
        return writer.finish(Protocol.MAX_SESSION_REQUEST_BYTES);
    }

    static SessionRequest decode(byte[] encoded) {
        Wire.Reader reader = new Wire.Reader(encoded, Protocol.MAX_SESSION_REQUEST_BYTES);
        reader.magic(Wire.REQUEST_MAGIC);
        Wire.requireVersion(reader.u16());
        SessionRequest request = new SessionRequest(
                reader.i64(),
                reader.bytes(Protocol.NONCE_BYTES),
                reader.string("productId", 128),
                reader.string("channel", 64),
                reader.string("loaderVersion", 64),
                reader.string("installationId", 128),
                reader.string("license", 4096));
        reader.finish();
        return request;
    }
}
