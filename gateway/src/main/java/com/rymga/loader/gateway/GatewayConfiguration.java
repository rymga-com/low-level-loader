package com.rymga.loader.gateway;

import java.nio.file.Path;

import org.eclipse.microprofile.config.inject.ConfigProperty;

import com.rymga.loader.protocol.Protocol;

import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.Dependent;
import jakarta.inject.Inject;

@Dependent
final class GatewayConfiguration {
    @Inject
    @ConfigProperty(name = "loader.license-provider.mode")
    String licenseProviderMode;

    @Inject
    @ConfigProperty(name = "loader.mock.enabled", defaultValue = "false")
    boolean mockEnabled;

    @Inject
    @ConfigProperty(name = "loader.mock-license", defaultValue = "unconfigured")
    String mockLicense;

    @Inject
    @ConfigProperty(name = "loader.license-provider.url", defaultValue = "unconfigured")
    String licenseProviderUrl;

    @Inject
    @ConfigProperty(name = "loader.license-provider.credential", defaultValue = "unconfigured")
    String licenseProviderCredential;

    @Inject
    @ConfigProperty(name = "loader.license-provider.timeout-millis", defaultValue = "3000")
    long licenseProviderTimeoutMillis;

    @Inject
    @ConfigProperty(name = "loader.signing.key-id")
    String keyId;

    @Inject
    @ConfigProperty(name = "loader.signing.private-key-pkcs8-base64")
    String privateKeyPkcs8Base64;

    @Inject
    @ConfigProperty(name = "loader.signing.public-key-x509-base64")
    String publicKeyX509Base64;

    @Inject
    @ConfigProperty(name = "loader.artifact.root")
    Path artifactRoot;

    @Inject
    @ConfigProperty(name = "loader.artifact.product-id")
    String artifactProductId;

    @Inject
    @ConfigProperty(name = "loader.artifact.channel")
    String artifactChannel;

    @Inject
    @ConfigProperty(name = "loader.artifact.id")
    String artifactId;

    @Inject
    @ConfigProperty(name = "loader.artifact.filename")
    String artifactFilename;

    @Inject
    @ConfigProperty(name = "loader.artifact.max-bytes", defaultValue = "2147483648")
    long maxArtifactBytes;

    @Inject
    @ConfigProperty(name = "loader.session.ttl-seconds", defaultValue = "60")
    long sessionTtlSeconds;

    @Inject
    @ConfigProperty(name = "loader.session.clock-skew-seconds", defaultValue = "120")
    long clockSkewSeconds;

    @PostConstruct
    void validateSecurityLimits() {
        if (maxArtifactBytes < 1 || maxArtifactBytes > Protocol.MAX_ARTIFACT_BYTES) {
            throw new IllegalStateException("loader.artifact.max-bytes must be between 1 and " + Protocol.MAX_ARTIFACT_BYTES);
        }
        if (sessionTtlSeconds < 1 || sessionTtlSeconds > 300) {
            throw new IllegalStateException("loader.session.ttl-seconds must be between 1 and 300");
        }
        if (clockSkewSeconds < 0 || clockSkewSeconds > 300) {
            throw new IllegalStateException("loader.session.clock-skew-seconds must be between 0 and 300");
        }
    }
}
