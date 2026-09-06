package com.rymga.loader.gateway;

import static org.junit.jupiter.api.Assertions.assertThrows;

import java.security.KeyPairGenerator;
import java.util.Base64;

import org.junit.jupiter.api.Test;

class SigningKeysTest {
    @Test
    void mismatchedKeyPairFailsAtStartup() throws Exception {
        var generator = KeyPairGenerator.getInstance("Ed25519");
        var first = generator.generateKeyPair();
        var second = generator.generateKeyPair();
        GatewayConfiguration configuration = new GatewayConfiguration();
        configuration.privateKeyPkcs8Base64 = Base64.getEncoder().encodeToString(first.getPrivate().getEncoded());
        configuration.publicKeyX509Base64 = Base64.getEncoder().encodeToString(second.getPublic().getEncoded());
        SigningKeys keys = new SigningKeys();
        keys.configuration = configuration;
        assertThrows(IllegalStateException.class, keys::load);
    }
}
