package com.rymga.loader.gateway;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertThrows;

import org.junit.jupiter.api.Test;

final class GatewayConfigurationTest {
    @Test
    void securityLimitsFailClosed() {
        GatewayConfiguration configuration = new GatewayConfiguration();
        configuration.maxArtifactBytes = 1;
        configuration.sessionTtlSeconds = 60;
        configuration.clockSkewSeconds = 120;
        assertDoesNotThrow(configuration::validateSecurityLimits);

        configuration.sessionTtlSeconds = 301;
        assertThrows(IllegalStateException.class, configuration::validateSecurityLimits);
        configuration.sessionTtlSeconds = 60;
        configuration.clockSkewSeconds = -1;
        assertThrows(IllegalStateException.class, configuration::validateSecurityLimits);
        configuration.clockSkewSeconds = 120;
        configuration.maxArtifactBytes = 0;
        assertThrows(IllegalStateException.class, configuration::validateSecurityLimits);
    }
}
