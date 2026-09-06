package com.rymga.loader.gateway;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.ByteArrayInputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.http.HttpClient;
import java.nio.charset.StandardCharsets;
import java.security.KeyFactory;
import java.security.KeyStore;
import java.security.PrivateKey;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.spec.PKCS8EncodedKeySpec;
import java.time.Duration;
import java.util.Base64;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManagerFactory;

import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import com.sun.net.httpserver.HttpsConfigurator;
import com.sun.net.httpserver.HttpsServer;

class LicenseValidatorTest {
    private static final String PRIVATE_KEY = """
            -----BEGIN PRIVATE KEY-----
            MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQC6mNb+rWHHCKPm
            kTI9tFaVfpVdzBXyDMhhxtb8JC4KPm600an8nOPdqS/YOQ/HDXUhuSJhd0Shkw7Z
            YBjcr8MrOQGjZ2XgUh5XVhe7u/r9k9YdKAJ4DfmLmWPTZ6AvP3l0Xtg9nveEhtEm
            T6YHHsS7hxmaEjoTPd7GDRwZwgLZrwmFe7CsPRD5zAvAjyb5hd2Rua5sl7Bu/K/R
            jDwN7CibPLZznY/x+QVsJdo1p47SPLcxM9kLQr73EynYCIxS+HmdiJzf0wAfQ1sz
            sgW1a4qQwcYslkd2MCLvtPtD4JOhdrm4JsLURAoqDO36xVmucfRWcIt+Ifba+Brm
            AoAGlqLhAgMBAAECggEAFrarGQYu7ETjepn7BSLSFVF6yhehJj0jGS/UlI2s4wdV
            f4I0EHFilWVWUxojDwZW1zuLcVTM0GQvvwshC6ADgFvolKTt9D/avKPYsLwBoeNg
            gV0Q6987nKAX1DB9gbLWzHENHncexw2IIQM6AQzgQrl/xs7oLdZuqyoGXSZOJYQg
            3kTW49KG0B5jTcSwE8DcHFKH+YHJopHRnP6k6aXPeqr9t0ETwgll1kc/yjmWqTYw
            0cwulUzCLoGNudv3f0pi2KfD8/eLOizTmi/yFK5XAdyTr3YwrP/2E6NNbIzvQcQx
            vUaeUk+kc8jmJlvkBrdlXBc0niCY8BjNj7enG92PPwKBgQD4U2NZ0NTI7GVsH1oo
            yVl3fdbmCPtjdhDreI3/GJX/D46OmLIbsRyUU9A7nvmM4UHSdkq1XF5PjwazoTnW
            mIXmiVDTdAMIZCVwXaOJGMJ1dRsxHrtqvJGP+4ZT4vrqtQUX/CI/ZA4O9fkIeHKd
            ltKd8rrsh8XRoD7HkqVxnrvZ2wKBgQDAXRbfw9LdaWnJdFAhVDJfNFtN4iJOFYPO
            A0WT5cQAzsgvQpgNGHhz3X6ELdxdmPYUNUE3BKR+9jNxqPp4M/oiql1YT8P95rnN
            iY1IZaTv0Asae/HNZViVLlFPLuOUhhf0k45vKNfiWhPNZqj7IVDwQz+BjpbWHuTZ
            0LN/N+gI8wKBgQDKzq/RZrCh+A9NIr3rmaYr5OZhsOw+6uoepbKyqE24pefzpdmw
            rBF/QoRKbSe6Wy36yQly3SFZMKJ0ijRGgwK4qWUNjS386G17O76X9VS7wQyYLVU7
            cw4e3XlzgWkTzwt97zm5M+oXZeQhet2AvvD8doUbMDfrYEhljer4xH7/CQKBgQCK
            4+u7HvHmSdFG0oN0vQ0trmjqQAdS57fmDHi1Q6WUi7kAXo3Yjr2RsvBBUbeVoPa0
            OotexSxcOzmDrGVEWwsU8ns5Y0Z40ZmALPvktPc067rIoYtCKRWfiPTOPmW1fGhZ
            gBzMZ+oQFcfMe34w7Ko6/+MfkSuZaG8GUej4Rw5zPwKBgGpU321W9d/JjYPdPVvO
            ULvk48gK0JRyYuMlqozXdTVR1qc2HsOO/eqVfWC2lvGCiUxKC0dkN/dtsEDbWfAd
            6SYctMnhoRHp0pMLRhi3/PAmbXcUXH61JwPSf6xg7FC/vkMcbrVgKXF3nRn68yNk
            4EeWsX0CrfQAhHR6JTB3qwc/
            -----END PRIVATE KEY-----
            """;
    private static final String CERTIFICATE = """
            -----BEGIN CERTIFICATE-----
            MIIDJTCCAg2gAwIBAgIUb/Le295bH61AsqZGwdMU9OiYiI0wDQYJKoZIhvcNAQEL
            BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDkwMzIxMTMwNloXDTM2MDgz
            MTIxMTMwNlowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF
            AAOCAQ8AMIIBCgKCAQEAupjW/q1hxwij5pEyPbRWlX6VXcwV8gzIYcbW/CQuCj5u
            tNGp/Jzj3akv2DkPxw11IbkiYXdEoZMO2WAY3K/DKzkBo2dl4FIeV1YXu7v6/ZPW
            HSgCeA35i5lj02egLz95dF7YPZ73hIbRJk+mBx7Eu4cZmhI6Ez3exg0cGcIC2a8J
            hXuwrD0Q+cwLwI8m+YXdkbmubJewbvyv0Yw8Dewomzy2c52P8fkFbCXaNaeO0jy3
            MTPZC0K+9xMp2AiMUvh5nYic39MAH0NbM7IFtWuKkMHGLJZHdjAi77T7Q+CToXa5
            uCbC1EQKKgzt+sVZrnH0VnCLfiH22vga5gKABpai4QIDAQABo28wbTAdBgNVHQ4E
            FgQU14G4txKGZdYQ2V9lxxICEmiYnPkwHwYDVR0jBBgwFoAU14G4txKGZdYQ2V9l
            xxICEmiYnPkwDwYDVR0TAQH/BAUwAwEB/zAaBgNVHREEEzARgglsb2NhbGhvc3SH
            BH8AAAEwDQYJKoZIhvcNAQELBQADggEBABD4sj/3aJeYkH+rJ8tcwTiB47qJ+qtN
            uO8I9tu2oEzmuka2lwB349c4bTq2PdFQrjBmpCZL/1672zZ8CVtD1PuCj2xLRhhJ
            F/CBU7Ku5u4gUVi3Lh8+l/DfBc1z1A8OpvgpS0FvU74rG4rZGv+KtvsGsWwH7FYd
            khzZkXCTqYI8ZiusRgPGDvGPgWrCw+v5Rq//bXlcN9BXlFdL+BMVCdZxCwXwJr2E
            fvF8iscnDbuuOVCy7uxw2P5bbw104E22tEw3Hmp/HYD7HBpXzrQS4AlZyu4n16+n
            RAikQMHH8aaxBK/tZ0aPFo/AD9IN1j0qhp296nY9OWoFMFMat6fxxfs=
            -----END CERTIFICATE-----
            """;

    private static HttpsServer server;
    private static ExecutorService executor;
    private static volatile Reply reply;
    private static volatile String authorization;
    private static volatile String requestBody;

    @BeforeAll
    static void startProvider() throws Exception {
        SSLContext serverContext = sslContext(true);
        server = HttpsServer.create(new InetSocketAddress(InetAddress.getLoopbackAddress(), 0), 0);
        server.setHttpsConfigurator(new HttpsConfigurator(serverContext));
        executor = Executors.newCachedThreadPool();
        server.setExecutor(executor);
        server.createContext("/verify", exchange -> {
            authorization = exchange.getRequestHeaders().getFirst("Authorization");
            requestBody = new String(exchange.getRequestBody().readAllBytes(), StandardCharsets.UTF_8);
            Reply current = reply;
            if (current.delayMillis > 0) {
                try { Thread.sleep(current.delayMillis); } catch (InterruptedException exception) { Thread.currentThread().interrupt(); }
            }
            byte[] body = current.body.getBytes(StandardCharsets.UTF_8);
            exchange.getResponseHeaders().set("Content-Type", "application/json");
            exchange.sendResponseHeaders(current.status, body.length);
            exchange.getResponseBody().write(body);
            exchange.close();
        });
        server.start();
    }

    @AfterAll
    static void stopProvider() {
        server.stop(0);
        executor.shutdownNow();
    }

    @Test
    void validAndInvalidProviderRepliesArePassedThrough() throws Exception {
        reply = new Reply(200, "{\"valid\":true}", 0);
        assertTrue(validator(1_000).accepts(request()));
        assertEquals("Bearer provider-secret", authorization);
        assertTrue(requestBody.contains("\"license\":\"license\""));

        reply = new Reply(200, " { \"valid\" : false } ", 0);
        assertFalse(validator(1_000).accepts(request()));
    }

    @Test
    void providerFailuresAndAmbiguousJsonFailClosed() throws Exception {
        reply = new Reply(500, "server error", 0);
        assertUnavailable(validator(1_000));

        reply = new Reply(200, "{\"valid\":true,\"valid\":false}", 0);
        assertUnavailable(validator(1_000));

        reply = new Reply(200, "{\"message\":\"valid: true\"}", 0);
        assertUnavailable(validator(1_000));

        assertThrows(LicenseValidator.LicenseProviderUnavailableException.class,
                () -> LicenseValidator.providerResponseIsAccepted(200, new byte[8_193]));
        assertThrows(LicenseValidator.LicenseProviderUnavailableException.class,
                () -> LicenseValidator.providerResponseIsAccepted(200, new byte[]{(byte) 0xc3, 0x28}));
    }

    @Test
    void providerTimeoutFailsClosed() throws Exception {
        reply = new Reply(200, "{\"valid\":true}", 300);
        assertUnavailable(validator(50));
    }

    private static void assertUnavailable(LicenseValidator validator) {
        assertThrows(LicenseValidator.LicenseProviderUnavailableException.class, () -> validator.accepts(request()));
    }

    private static LicenseValidator validator(long timeoutMillis) throws Exception {
        GatewayConfiguration configuration = new GatewayConfiguration();
        configuration.licenseProviderMode = "http";
        configuration.licenseProviderUrl = "https://localhost:" + server.getAddress().getPort() + "/verify";
        configuration.licenseProviderCredential = "provider-secret";
        configuration.licenseProviderTimeoutMillis = timeoutMillis;
        LicenseValidator validator = new LicenseValidator();
        validator.configuration = configuration;
        validator.client = HttpClient.newBuilder().sslContext(sslContext(false)).connectTimeout(Duration.ofMillis(timeoutMillis)).build();
        validator.validateConfiguration();
        return validator;
    }

    private static LicenseValidator.SessionRequestData request() {
        return new LicenseValidator.SessionRequestData("demo-plugin", "test-installation", "license", 42, new byte[32]);
    }

    private static SSLContext sslContext(boolean server) throws Exception {
        Certificate certificate = CertificateFactory.getInstance("X.509").generateCertificate(
                new ByteArrayInputStream(CERTIFICATE.getBytes(StandardCharsets.US_ASCII)));
        KeyStore trustStore = KeyStore.getInstance(KeyStore.getDefaultType());
        trustStore.load(null); trustStore.setCertificateEntry("provider", certificate);
        TrustManagerFactory trust = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        trust.init(trustStore);
        SSLContext context = SSLContext.getInstance("TLS");
        if (!server) { context.init(null, trust.getTrustManagers(), null); return context; }
        PrivateKey key = KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(pem(PRIVATE_KEY)));
        KeyStore keys = KeyStore.getInstance(KeyStore.getDefaultType());
        keys.load(null); keys.setKeyEntry("provider", key, new char[0], new Certificate[]{certificate});
        KeyManagerFactory managers = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());
        managers.init(keys, new char[0]); context.init(managers.getKeyManagers(), null, null); return context;
    }

    private static byte[] pem(String value) {
        return Base64.getMimeDecoder().decode(value.replaceAll("-----[^-]+-----", ""));
    }

    private record Reply(int status, String body, long delayMillis) {
    }
}
