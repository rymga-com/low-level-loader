package com.rymga.loader.protocol;

import java.security.GeneralSecurityException;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;

final class Ed25519 {
    private Ed25519() {
    }

    static byte[] sign(PrivateKey privateKey, byte[] message) {
        try {
            Signature signature = Signature.getInstance("Ed25519");
            signature.initSign(privateKey);
            signature.update(message);
            return signature.sign();
        } catch (GeneralSecurityException exception) {
            throw new IllegalStateException("could not sign manifest", exception);
        }
    }

    static boolean verify(PublicKey publicKey, byte[] message, byte[] signatureBytes) {
        try {
            Signature signature = Signature.getInstance("Ed25519");
            signature.initVerify(publicKey);
            signature.update(message);
            return signature.verify(signatureBytes);
        } catch (GeneralSecurityException exception) {
            throw new IllegalStateException("could not verify manifest", exception);
        }
    }
}
