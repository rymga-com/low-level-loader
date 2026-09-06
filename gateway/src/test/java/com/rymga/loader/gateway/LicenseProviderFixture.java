package com.rymga.loader.gateway;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyFactory;
import java.security.KeyStore;
import java.security.PrivateKey;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.Base64;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;

import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpsConfigurator;
import com.sun.net.httpserver.HttpsServer;

public final class LicenseProviderFixture {
    private LicenseProviderFixture() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) throw new IllegalArgumentException("expected certificate and PKCS#8 private key");
        HttpsServer server = HttpsServer.create(new InetSocketAddress("localhost", 9443), 0);
        server.setHttpsConfigurator(new HttpsConfigurator(sslContext(Path.of(args[0]), Path.of(args[1]))));
        server.createContext("/health", exchange -> respond(exchange, 200, "ok"));
        server.createContext("/v1/verify", LicenseProviderFixture::verify);
        server.setExecutor(Executors.newCachedThreadPool());
        server.start();
        new CountDownLatch(1).await();
    }

    private static void verify(HttpExchange exchange) throws IOException {
        byte[] body = exchange.getRequestBody().readNBytes(8_193);
        String json = new String(body, StandardCharsets.UTF_8);
        boolean accepted = "POST".equals(exchange.getRequestMethod()) && body.length <= 8_192
                && "Bearer provider-test-credential".equals(exchange.getRequestHeaders().getFirst("Authorization"))
                && json.contains("\"license\":\"dev-license\"")
                && json.contains("\"product\":\"demo-plugin\"");
        respond(exchange, 200, accepted ? "{\"valid\":true}" : "{\"valid\":false}");
    }

    private static void respond(HttpExchange exchange, int status, String body) throws IOException {
        byte[] encoded = body.getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().set("Content-Type", "application/json");
        exchange.sendResponseHeaders(status, encoded.length);
        try (var output = exchange.getResponseBody()) {
            output.write(encoded);
        }
    }

    private static SSLContext sslContext(Path certificatePath, Path privateKeyPath) throws Exception {
        X509Certificate certificate;
        try (var input = Files.newInputStream(certificatePath)) {
            certificate = (X509Certificate) CertificateFactory.getInstance("X.509").generateCertificate(input);
        }
        String pem = Files.readString(privateKeyPath, StandardCharsets.US_ASCII)
                .replace("-----BEGIN PRIVATE KEY-----", "")
                .replace("-----END PRIVATE KEY-----", "")
                .replaceAll("\\s", "");
        PrivateKey privateKey = KeyFactory.getInstance("RSA").generatePrivate(
                new PKCS8EncodedKeySpec(Base64.getDecoder().decode(pem)));
        KeyStore keys = KeyStore.getInstance("PKCS12");
        keys.load(null, null);
        keys.setKeyEntry("provider", privateKey, new char[0], new java.security.cert.Certificate[]{certificate});
        KeyManagerFactory managers = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());
        managers.init(keys, new char[0]);
        SSLContext context = SSLContext.getInstance("TLS");
        context.init(managers.getKeyManagers(), null, null);
        return context;
    }
}
