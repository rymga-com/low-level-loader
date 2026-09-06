package com.rymga.loader.protocol;

import java.util.Arrays;

public final class ProtocolSelfTest {
    private static final String REQUEST_VECTOR = "524c535200010000000066d16940000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f000b64656d6f2d706c7567696e0006737461626c650005302e312e30000c696e7374616c6c2d3132333400144c4943454e53452d4558414d504c452d31323334";
    private static final String MANIFEST_UNSIGNED_VECTOR = "524c534d000100096d61696e2d32303236000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f0000000066d169400000000066d1697c000b64656d6f2d706c7567696e0006737461626c65000f72656c656173652d323032362d3039000f64656d6f2d706c7567696e2e6a6172000000000000a7eba0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf";
    private static final String MANIFEST_SIGNATURE_VECTOR = "2d6632c24a2cd7b49d491a6d1091cc20b83c549a1fcc73d9e8cd9126580be4824458e9fc271ca9d402eeac54fc441ba0078c8444f2ba60bf8c477bba8696e90a";

    private ProtocolSelfTest() {
    }

    public static void main(String[] args) {
        requestRoundTripIsCanonical();
        signedManifestRoundTripVerifies();
        malformedMessagesFailClosed();
        System.out.println("protocol self-test: OK");
    }

    private static void requestRoundTripIsCanonical() {
        SessionRequest request = ProtocolFixtures.request();
        byte[] encoded = request.encode();
        require(REQUEST_VECTOR.equals(ProtocolFixtures.hex(encoded)), "session request vector changed");
        SessionRequest decoded = SessionRequest.decode(encoded);
        require(Arrays.equals(encoded, decoded.encode()), "request must round-trip byte-for-byte");
        require(Arrays.equals(request.requestNonce(), decoded.requestNonce()), "request nonce changed");
    }

    private static void signedManifestRoundTripVerifies() {
        SessionManifest signed = ProtocolFixtures.unsignedManifest().sign(ProtocolFixtures.privateKey());
        require(MANIFEST_UNSIGNED_VECTOR.equals(ProtocolFixtures.hex(signed.unsignedBytes())),
                "unsigned manifest vector changed");
        require(MANIFEST_SIGNATURE_VECTOR.equals(ProtocolFixtures.hex(signed.signature())),
                "manifest signature vector changed");
        require(signed.verify(ProtocolFixtures.publicKey()), "valid signature rejected");

        SessionManifest decoded = SessionManifest.decode(signed.encode());
        require(decoded.verify(ProtocolFixtures.publicKey()), "decoded signature rejected");
        require(Arrays.equals(signed.encode(), decoded.encode()), "manifest must round-trip byte-for-byte");

        byte[] alteredSignature = decoded.signature();
        alteredSignature[0] ^= 1;
        SessionManifest altered = SessionManifest.decoded(decoded.keyId(), decoded.requestNonce(), decoded.sessionId(),
                decoded.issuedAt(), decoded.expiresAt(), decoded.productId(), decoded.channel(), decoded.artifactId(),
                decoded.originalFilename(), decoded.artifactSize(), decoded.artifactSha256(), alteredSignature);
        require(!altered.verify(ProtocolFixtures.publicKey()), "tampered signature accepted");
    }

    private static void malformedMessagesFailClosed() {
        byte[] requestWithTrailingData = Arrays.copyOf(ProtocolFixtures.request().encode(),
                ProtocolFixtures.request().encode().length + 1);
        expectProtocolFailure(() -> SessionRequest.decode(requestWithTrailingData));
        expectProtocolFailure(() -> SessionManifest.unsigned("main", new byte[32], new byte[32], 10, 11,
                "demo", "stable", "release", "../escape.jar", 1, new byte[32]));
        expectProtocolFailure(() -> SessionManifest.unsigned("main", new byte[32], new byte[32], 10, 11,
                "demo", "stable", "release", "safe.jar:stream.jar", 1, new byte[32]));
        expectProtocolFailure(() -> SessionManifest.unsigned("main", new byte[32], new byte[32], 10, 11,
                "demo", "stable", "release", "CON.jar", 1, new byte[32]));
        expectProtocolFailure(() -> new SessionRequest(1, new byte[32], "demo", "stable", "1", "id",
                "LICENSE\nINJECTION"));
    }

    private static void expectProtocolFailure(Runnable action) {
        try {
            action.run();
            throw new AssertionError("expected ProtocolException");
        } catch (ProtocolException expected) {
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }
}
