package com.rymga.loader.gateway;

import java.security.SecureRandom;
import java.time.Instant;

import com.rymga.loader.protocol.ProtocolException;
import com.rymga.loader.protocol.SessionManifest;
import com.rymga.loader.protocol.SessionRequest;

import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

@ApplicationScoped
final class SessionService {
    private static final SecureRandom RANDOM = new SecureRandom();

    @Inject
    GatewayConfiguration configuration;

    @Inject
    LicenseValidator licenseValidator;

    @Inject
    ArtifactCatalog catalog;

    @Inject
    SigningKeys signingKeys;

    SessionManifest issue(SessionRequest request) {
        long now = Instant.now().getEpochSecond();
        if (request.timestamp() < now - configuration.clockSkewSeconds
                || request.timestamp() > now + configuration.clockSkewSeconds) {
            throw new ProtocolException("request timestamp outside the accepted window");
        }
        if (!licenseValidator.accepts(new LicenseValidator.SessionRequestData(request.productId(), request.installationId(),
                request.license(), request.timestamp(), request.requestNonce()))) {
            throw new LicenseDeniedException();
        }

        ArtifactCatalog.Artifact artifact = catalog.resolve(request.productId(), request.channel());
        byte[] sessionId = new byte[32];
        RANDOM.nextBytes(sessionId);

        return SessionManifest.unsigned(signingKeys.keyId(), request.requestNonce(), sessionId, now,
                now + configuration.sessionTtlSeconds, request.productId(), request.channel(), artifact.id(),
                artifact.filename(), artifact.size(), artifact.sha256()).sign(signingKeys.privateKey());
    }

    static final class LicenseDeniedException extends RuntimeException {
    }
}
