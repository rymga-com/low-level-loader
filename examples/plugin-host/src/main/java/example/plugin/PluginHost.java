package example.plugin;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ServiceLoader;

public final class PluginHost {
    private PluginHost() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("expected plugin directory");
        try (var jars = Files.list(Path.of(args[0]))) {
            for (Path jar : jars.filter(path -> path.getFileName().toString().endsWith(".jar")).toList()) {
                try (URLClassLoader loader = new URLClassLoader(new URL[]{jar.toUri().toURL()}, Plugin.class.getClassLoader())) {
                    ServiceLoader.load(Plugin.class, loader).forEach(Plugin::start);
                }
            }
        }
    }
}
