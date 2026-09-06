package com.rymga.loader.protocol;

import java.security.KeyFactory;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.HexFormat;

final class ProtocolFixtures {
    private static final HexFormat HEX = HexFormat.of();
    private static final byte[] PRIVATE_KEY = HEX.parseHex(
            "302e020100300506032b657004220420"
                    + "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    private static final byte[] PUBLIC_KEY = HEX.parseHex(
            "302a300506032b6570032100"
                    + "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");

    private ProtocolFixtures() {
    }

    static SessionRequest request() {
        return new SessionRequest(
                1_725_000_000L,
                HEX.parseHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"),
                "demo-plugin",
                "stable",
                "0.1.0",
                "install-1234",
                "LICENSE-EXAMPLE-1234");
    }

    static SessionManifest unsignedManifest() {
        return SessionManifest.unsigned(
                "main-2026",
                request().requestNonce(),
                HEX.parseHex("202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"),
                1_725_000_000L,
                1_725_000_060L,
                "demo-plugin",
                "stable",
                "release-2026-09",
                "demo-plugin.jar",
                42_987L,
                HEX.parseHex("a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf"));
    }

    static PrivateKey privateKey() {
        try {
            return KeyFactory.getInstance("Ed25519").generatePrivate(new PKCS8EncodedKeySpec(PRIVATE_KEY));
        } catch (Exception exception) {
            throw new AssertionError(exception);
        }
    }

    static PublicKey publicKey() {
        try {
            return KeyFactory.getInstance("Ed25519").generatePublic(new X509EncodedKeySpec(PUBLIC_KEY));
        } catch (Exception exception) {
            throw new AssertionError(exception);
        }
    }

    static String hex(byte[] bytes) {
        return HEX.formatHex(bytes);
    }
}
