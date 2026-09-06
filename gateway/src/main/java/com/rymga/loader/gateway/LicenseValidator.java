package com.rymga.loader.gateway;

import java.io.InputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.charset.CodingErrorAction;
import java.security.MessageDigest;
import java.time.Duration;
import java.util.regex.Pattern;

import io.quarkus.runtime.Startup;
import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

@ApplicationScoped
@Startup
final class LicenseValidator {
    private static final Pattern VALID_RESPONSE = Pattern.compile("\\s*\\{\\s*\"valid\"\\s*:\\s*(true|false)\\s*}\\s*");

    @Inject
    GatewayConfiguration configuration;

    HttpClient client;
    private URI providerUri;

    @PostConstruct
    void validateConfiguration() {
        if ("mock".equals(configuration.licenseProviderMode)) {
            if (!configuration.mockEnabled || configuration.mockLicense.isBlank() || "unconfigured".equals(configuration.mockLicense)) {
                throw new IllegalStateException("the mock license provider requires explicit enablement and license");
            }
            return;
        }
        if (!"http".equals(configuration.licenseProviderMode)
                || configuration.licenseProviderCredential.isBlank()
                || "unconfigured".equals(configuration.licenseProviderCredential)
                || configuration.licenseProviderTimeoutMillis < 1 || configuration.licenseProviderTimeoutMillis > 30_000) {
            throw new IllegalStateException("a configured HTTPS license provider is required");
        }
        providerUri = URI.create(configuration.licenseProviderUrl);
        if (!"https".equals(providerUri.getScheme()) || providerUri.getHost() == null ||
                providerUri.getUserInfo() != null || providerUri.getFragment() != null) {
            throw new IllegalStateException("loader.license-provider.url must be an HTTPS URL");
        }
        if (client == null) {
            client = HttpClient.newBuilder()
                    .connectTimeout(Duration.ofMillis(configuration.licenseProviderTimeoutMillis))
                    .build();
        }
    }

    boolean accepts(SessionRequestData request) {
        if ("mock".equals(configuration.licenseProviderMode)) {
            return MessageDigest.isEqual(configuration.mockLicense.getBytes(StandardCharsets.UTF_8),
                    request.license().getBytes(StandardCharsets.UTF_8));
        }
        try {
            HttpRequest providerRequest = HttpRequest.newBuilder(providerUri)
                    .timeout(Duration.ofMillis(configuration.licenseProviderTimeoutMillis))
                    .header("Authorization", "Bearer " + configuration.licenseProviderCredential)
                    .header("Content-Type", "application/json")
                    .POST(HttpRequest.BodyPublishers.ofString(request.json(), StandardCharsets.UTF_8))
                    .build();
            HttpResponse<InputStream> response = client.send(providerRequest, HttpResponse.BodyHandlers.ofInputStream());
            try (InputStream body = response.body()) {
                return providerResponseIsAccepted(response.statusCode(), body.readNBytes(8_193));
            }
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw new LicenseProviderUnavailableException();
        } catch (Exception exception) {
            throw new LicenseProviderUnavailableException();
        }
    }

    static boolean providerResponseIsAccepted(int statusCode, byte[] body) {
        if (statusCode != 200 || body.length > 8_192) throw new LicenseProviderUnavailableException();
        String decoded;
        try {
            decoded = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(java.nio.ByteBuffer.wrap(body)).toString();
        } catch (java.nio.charset.CharacterCodingException exception) {
            throw new LicenseProviderUnavailableException();
        }
        var valid = VALID_RESPONSE.matcher(decoded);
        if (!valid.matches()) throw new LicenseProviderUnavailableException();
        return Boolean.parseBoolean(valid.group(1));
    }

    record SessionRequestData(String product, String installationId, String license, long timestamp, byte[] nonce) {
        String json() {
            return "{\"license\":\"" + escape(license) + "\",\"product\":\"" + escape(product)
                    + "\",\"installationId\":\"" + escape(installationId)
                    + "\",\"timestamp\":" + timestamp + ",\"nonce\":\""
                    + java.util.Base64.getUrlEncoder().withoutPadding().encodeToString(nonce) + "\"}";
        }

        private static String escape(String value) {
            StringBuilder escaped = new StringBuilder(value.length() + 16);
            for (int i = 0; i < value.length(); i++) {
                char character = value.charAt(i);
                switch (character) {
                    case '"' -> escaped.append("\\\"");
                    case '\\' -> escaped.append("\\\\");
                    case '\b' -> escaped.append("\\b");
                    case '\f' -> escaped.append("\\f");
                    case '\n' -> escaped.append("\\n");
                    case '\r' -> escaped.append("\\r");
                    case '\t' -> escaped.append("\\t");
                    default -> {
                        if (character < 0x20) escaped.append(String.format("\\u%04x", (int) character));
                        else escaped.append(character);
                    }
                }
            }
            return escaped.toString();
        }
    }

    static final class LicenseProviderUnavailableException extends RuntimeException {
    }
}
