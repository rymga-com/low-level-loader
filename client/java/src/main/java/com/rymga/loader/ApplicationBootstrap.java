package com.rymga.loader;

import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.concurrent.TimeUnit;

public final class ApplicationBootstrap {
    private ApplicationBootstrap() {
    }

    public static void main(String[] arguments) {
        int status;
        try {
            status = run(home(), arguments);
        } catch (Exception exception) {
            System.err.println("rymga-loader: " + exception.getMessage());
            status = 1;
        }
        System.exit(status);
    }

    static int run(Path home, String[] arguments) throws IOException {
        Path config = home.resolve("config.yml");
        if (ApplicationConfig.create(config)) {
            System.err.println("rymga-loader: config.yml created; set license.key and run again");
            return 2;
        }

        char[] license = ApplicationConfig.read(config);
        Path data = home.resolve(".rymga-loader");
        try {
            NativeLoader.load(BundledNativeLibrary.extract(data));
            Path ca = home.resolve("loader-ca.pem");
            try (NativeArtifact artifact = NativeLoader.acquireEmbedded(data.resolve("runtime"),
                    Files.isRegularFile(ca) ? ca : null, license)) {
                return launch(artifact.path(), arguments);
            }
        } finally {
            Arrays.fill(license, '\0');
        }
    }

    private static int launch(Path artifact, String[] arguments) throws IOException {
        String executable = Path.of(System.getProperty("java.home"), "bin",
                System.getProperty("os.name").toLowerCase().contains("win") ? "java.exe" : "java").toString();
        var command = new ArrayList<String>(arguments.length + 3);
        command.add(executable);
        command.add("-jar");
        command.add(artifact.toString());
        command.addAll(Arrays.asList(arguments));
        Process process = new ProcessBuilder(command).inheritIO().start();
        try {
            return process.waitFor();
        } catch (InterruptedException exception) {
            process.destroy();
            try {
                if (!process.waitFor(5, TimeUnit.SECONDS)) process.destroyForcibly();
            } catch (InterruptedException ignored) {
                process.destroyForcibly();
            }
            Thread.currentThread().interrupt();
            return 130;
        }
    }

    private static Path home() throws IOException, URISyntaxException {
        Path jar = Path.of(ApplicationBootstrap.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        if (!Files.isRegularFile(jar)) throw new IOException("application bootstrap must run from its bundled JAR");
        return jar.toAbsolutePath().normalize().getParent();
    }
}
