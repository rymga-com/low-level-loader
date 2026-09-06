package com.rymga.loader.gateway;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;

import io.quarkus.runtime.Startup;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

@ApplicationScoped
@Startup
final class ArtifactCatalog {
    @Inject
    GatewayConfiguration configuration;

    private Artifact configured;

    @PostConstruct
    void load() {
        configured = loadConfiguredArtifact();
    }

    @PreDestroy
    void close() {
        if (configured != null) configured.close();
    }

    Artifact resolve(String productId, String channel) {
        if (!configuration.artifactProductId.equals(productId) || !configuration.artifactChannel.equals(channel)) {
            throw new ArtifactNotFoundException();
        }
        return configured;
    }

    Artifact resolveForManifest(String artifactId, long size, byte[] sha256) {
        Artifact artifact = configured;
        if (!artifact.id().equals(artifactId) || artifact.size() != size || !Arrays.equals(artifact.sha256(), sha256)) {
            throw new ArtifactNotFoundException();
        }
        return artifact;
    }

    private Artifact loadConfiguredArtifact() {
        FileChannel channel = null;
        try {
            Path root = configuration.artifactRoot.toAbsolutePath().normalize();
            Path requested = root.resolve(configuration.artifactFilename).normalize();
            if (!requested.startsWith(root) || !Files.isRegularFile(requested)) throw new ArtifactNotFoundException();
            Path realRoot = root.toRealPath();
            Path artifact = requested.toRealPath();
            if (!artifact.startsWith(realRoot)) throw new ArtifactUnavailableException();

            channel = FileChannel.open(artifact, StandardOpenOption.READ);
            long size = channel.size();
            if (size < 1 || size > configuration.maxArtifactBytes) throw new ArtifactUnavailableException();
            return new Artifact(configuration.artifactId, configuration.artifactFilename, size, sha256(channel, size), channel);
        } catch (ArtifactNotFoundException | ArtifactUnavailableException exception) {
            closeQuietly(channel);
            throw exception;
        } catch (IOException exception) {
            closeQuietly(channel);
            throw new ArtifactUnavailableException();
        }
    }

    private static byte[] sha256(FileChannel channel, long size) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            ByteBuffer buffer = ByteBuffer.allocate(32 * 1024);
            for (long position = 0; position < size;) {
                buffer.clear();
                int read = channel.read(buffer, position);
                if (read <= 0) throw new ArtifactUnavailableException();
                digest.update(buffer.array(), 0, read);
                position += read;
            }
            return digest.digest();
        } catch (NoSuchAlgorithmException exception) {
            throw new IllegalStateException("SHA-256 is unavailable", exception);
        } catch (IOException exception) {
            throw new ArtifactUnavailableException();
        }
    }

    private static void closeQuietly(FileChannel channel) {
        if (channel != null) try { channel.close(); } catch (IOException ignored) { }
    }

    record Artifact(String id, String filename, long size, byte[] sha256, FileChannel channel) {
        Artifact {
            sha256 = sha256.clone();
        }

        @Override
        public byte[] sha256() {
            return sha256.clone();
        }

        void writeTo(OutputStream output) throws IOException {
            ByteBuffer buffer = ByteBuffer.allocate(32 * 1024);
            for (long position = 0; position < size;) {
                buffer.clear();
                int read = channel.read(buffer, position);
                if (read <= 0) throw new IOException("artifact changed while streaming");
                output.write(buffer.array(), 0, read);
                position += read;
            }
        }

        void close() {
            closeQuietly(channel);
        }
    }

    static final class ArtifactNotFoundException extends RuntimeException {
    }

    static final class ArtifactUnavailableException extends RuntimeException {
    }
}
