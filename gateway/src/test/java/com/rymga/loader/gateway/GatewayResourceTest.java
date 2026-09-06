package com.rymga.loader.gateway;

import static org.hamcrest.Matchers.equalTo;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.InputStream;
import java.security.KeyFactory;
import java.security.PublicKey;
import java.security.spec.X509EncodedKeySpec;
import java.time.Instant;
import java.util.Base64;

import org.junit.jupiter.api.Test;

import com.rymga.loader.protocol.SessionManifest;
import com.rymga.loader.protocol.SessionRequest;

import io.quarkus.test.junit.QuarkusTest;
import io.restassured.RestAssured;
import jakarta.inject.Inject;

@QuarkusTest
public class GatewayResourceTest {
    private static final String PUBLIC_KEY = "MCowBQYDK2VwAyEA11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=";

    @Inject
    SigningKeys signingKeys;

    @Test
    void validLicenseReceivesSignedManifestAndExactArtifact() throws Exception {
        SessionRequest request = request("dev-license", Instant.now().getEpochSecond());
        byte[] encodedManifest = RestAssured.given()
                .contentType(GatewayResource.SESSION_REQUEST_MEDIA_TYPE)
                .body(request.encode())
                .post("/v1/session")
                .then()
                .statusCode(200)
                .extract()
                .asByteArray();

        SessionManifest manifest = SessionManifest.decode(encodedManifest);
        assertArrayEquals(request.requestNonce(), manifest.requestNonce());
        assertArrayEquals(publicKey().getEncoded(), signingKeys.publicKey().getEncoded());
        assertTrue(manifest.verify(publicKey()));

        byte[] artifact = RestAssured.given()
                .header("Authorization", "RymgaSession " + Base64.getUrlEncoder().withoutPadding()
                        .encodeToString(encodedManifest))
                .get("/v1/artifact")
                .then()
                .statusCode(200)
                .header("Cache-Control", equalTo("no-store"))
                .extract()
                .asByteArray();

        try (InputStream expected = GatewayResourceTest.class.getResourceAsStream("/artifacts/demo-plugin.jar")) {
            assertNotNull(expected);
            assertArrayEquals(expected.readAllBytes(), artifact);
        }
    }

    @Test
    void invalidLicenseDoesNotReceiveAManifest() {
        RestAssured.given()
                .contentType(GatewayResource.SESSION_REQUEST_MEDIA_TYPE)
                .body(request("wrong-license", Instant.now().getEpochSecond()).encode())
                .post("/v1/session")
                .then()
                .statusCode(403);
    }

    @Test
    void tamperedManifestCannotDownloadAnArtifact() {
        byte[] manifest = RestAssured.given()
                .contentType(GatewayResource.SESSION_REQUEST_MEDIA_TYPE)
                .body(request("dev-license", Instant.now().getEpochSecond()).encode())
                .post("/v1/session")
                .then()
                .statusCode(200)
                .extract()
                .asByteArray();
        manifest[20] ^= 1;

        RestAssured.given()
                .header("Authorization", "RymgaSession " + Base64.getUrlEncoder().withoutPadding().encodeToString(manifest))
                .get("/v1/artifact")
                .then()
                .statusCode(403);
    }

    @Test
    void expiredRequestFailsBeforeTheProviderDecision() {
        RestAssured.given()
                .contentType(GatewayResource.SESSION_REQUEST_MEDIA_TYPE)
                .body(request("dev-license", Instant.now().minusSeconds(121).getEpochSecond()).encode())
                .post("/v1/session")
                .then()
                .statusCode(400);
    }

    @Test
    void providerPayloadEscapesTheLicenseWithoutChangingTheNonce() {
        LicenseValidator.SessionRequestData data = new LicenseValidator.SessionRequestData("demo-plugin",
                "test-installation", "a\"b\\c", 42,
                new byte[]{0, 1, 2});
        assertEquals("{\"license\":\"a\\\"b\\\\c\",\"product\":\"demo-plugin\","
                + "\"installationId\":\"test-installation\",\"timestamp\":42,\"nonce\":\"AAEC\"}", data.json());
    }

    private static SessionRequest request(String license, long timestamp) {
        return new SessionRequest(timestamp,
                new byte[]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31},
                "demo-plugin", "stable", "test", "test-installation", license);
    }

    private static PublicKey publicKey() throws Exception {
        return KeyFactory.getInstance("Ed25519").generatePublic(
                new X509EncodedKeySpec(Base64.getDecoder().decode(PUBLIC_KEY)));
    }
}
