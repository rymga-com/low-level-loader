package com.rymga.loader.protocol;

public final class ProtocolVectorPrinter {
    private ProtocolVectorPrinter() {
    }

    public static void main(String[] args) {
        SessionManifest manifest = ProtocolFixtures.unsignedManifest().sign(ProtocolFixtures.privateKey());
        System.out.println("session-request=" + ProtocolFixtures.hex(ProtocolFixtures.request().encode()));
        System.out.println("manifest-unsigned=" + ProtocolFixtures.hex(manifest.unsignedBytes()));
        System.out.println("manifest-signature=" + ProtocolFixtures.hex(manifest.signature()));
        System.out.println("manifest-signed=" + ProtocolFixtures.hex(manifest.encode()));
    }
}
