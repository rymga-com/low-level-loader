package com.rymga.loader.gateway;

import java.security.KeyFactory;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.Base64;

import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

@ApplicationScoped
final class SigningKeys {
    @Inject
    GatewayConfiguration configuration;

    private PrivateKey privateKey;
    private PublicKey publicKey;

    @PostConstruct
    void load() {
        try {
            KeyFactory factory = KeyFactory.getInstance("Ed25519");
            privateKey = factory.generatePrivate(new PKCS8EncodedKeySpec(
                    Base64.getDecoder().decode(configuration.privateKeyPkcs8Base64)));
            publicKey = factory.generatePublic(new X509EncodedKeySpec(
                    Base64.getDecoder().decode(configuration.publicKeyX509Base64)));
            Signature signer = Signature.getInstance("Ed25519");
            signer.initSign(privateKey);
            signer.update(new byte[]{'R', 'L', 'S', 'K'});
            byte[] check = signer.sign();
            signer.initVerify(publicKey);
            signer.update(new byte[]{'R', 'L', 'S', 'K'});
            if (!signer.verify(check)) throw new IllegalStateException("Ed25519 private/public keys do not match");
        } catch (Exception exception) {
            throw new IllegalStateException("invalid Ed25519 signing-key configuration", exception);
        }
    }

    String keyId() {
        return configuration.keyId;
    }

    PrivateKey privateKey() {
        return privateKey;
    }

    PublicKey publicKey() {
        return publicKey;
    }
}
