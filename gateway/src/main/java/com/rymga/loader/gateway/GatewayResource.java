package com.rymga.loader.gateway;

import java.time.Instant;
import java.util.Base64;

import com.rymga.loader.protocol.ProtocolException;
import com.rymga.loader.protocol.SessionManifest;
import com.rymga.loader.protocol.SessionRequest;

import io.smallrye.common.annotation.Blocking;
import jakarta.inject.Inject;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.HeaderParam;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.Response;
import jakarta.ws.rs.core.StreamingOutput;

@Path("/v1")
public final class GatewayResource {
    static final String SESSION_REQUEST_MEDIA_TYPE = "application/vnd.rymga.loader-session-request";
    static final String SESSION_MANIFEST_MEDIA_TYPE = "application/vnd.rymga.loader-session-manifest";

    @Inject
    SessionService sessionService;

    @Inject
    SigningKeys signingKeys;

    @Inject
    ArtifactCatalog catalog;

    @POST
    @Path("/session")
    @Consumes(SESSION_REQUEST_MEDIA_TYPE)
    @Produces(SESSION_MANIFEST_MEDIA_TYPE)
    @Blocking
    public Response createSession(byte[] encodedRequest) {
        try {
            SessionRequest request = SessionRequest.decode(encodedRequest);
            SessionManifest manifest = sessionService.issue(request);
            return noStore(Response.ok(manifest.encode()).type(SESSION_MANIFEST_MEDIA_TYPE));
        } catch (ProtocolException exception) {
            return noStore(Response.status(Response.Status.BAD_REQUEST));
        } catch (SessionService.LicenseDeniedException exception) {
            return noStore(Response.status(Response.Status.FORBIDDEN));
        } catch (LicenseValidator.LicenseProviderUnavailableException exception) {
            return noStore(Response.status(Response.Status.SERVICE_UNAVAILABLE));
        } catch (ArtifactCatalog.ArtifactNotFoundException exception) {
            return noStore(Response.status(Response.Status.NOT_FOUND));
        } catch (ArtifactCatalog.ArtifactUnavailableException exception) {
            return noStore(Response.status(Response.Status.SERVICE_UNAVAILABLE));
        }
    }

    @GET
    @Path("/artifact")
    @Produces("application/java-archive")
    @Blocking
    public Response downloadArtifact(@HeaderParam("Authorization") String authorization) {
        try {
            SessionManifest manifest = decodeAuthorization(authorization);
            long now = Instant.now().getEpochSecond();
            if (!manifest.keyId().equals(signingKeys.keyId()) || !manifest.verify(signingKeys.publicKey())
                    || manifest.issuedAt() > now || manifest.expiresAt() < now) {
                return noStore(Response.status(Response.Status.FORBIDDEN));
            }

            ArtifactCatalog.Artifact artifact = catalog.resolveForManifest(manifest.artifactId(),
                    manifest.artifactSize(), manifest.artifactSha256());
            StreamingOutput stream = artifact::writeTo;
            return noStore(Response.ok(stream)
                    .type("application/java-archive")
                    .header("Content-Length", artifact.size()));
        } catch (IllegalArgumentException exception) {
            return noStore(Response.status(Response.Status.FORBIDDEN));
        } catch (ArtifactCatalog.ArtifactNotFoundException exception) {
            return noStore(Response.status(Response.Status.NOT_FOUND));
        } catch (ArtifactCatalog.ArtifactUnavailableException exception) {
            return noStore(Response.status(Response.Status.SERVICE_UNAVAILABLE));
        }
    }

    private static SessionManifest decodeAuthorization(String authorization) {
        String prefix = "RymgaSession ";
        if (authorization == null || !authorization.startsWith(prefix) || authorization.length() > 6_000) {
            throw new ProtocolException("invalid authorization header");
        }
        return SessionManifest.decode(Base64.getUrlDecoder().decode(authorization.substring(prefix.length())));
    }

    private static Response noStore(Response.ResponseBuilder response) {
        return response.header("Cache-Control", "no-store")
                .header("X-Content-Type-Options", "nosniff")
                .build();
    }
}
