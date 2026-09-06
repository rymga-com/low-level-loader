package com.rymga.loader.paper;

import java.io.IOException;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

import org.bukkit.event.EventHandler;
import org.bukkit.event.Listener;
import org.bukkit.event.server.PluginDisableEvent;
import org.bukkit.plugin.InvalidDescriptionException;
import org.bukkit.plugin.InvalidPluginException;
import org.bukkit.plugin.Plugin;
import org.bukkit.plugin.UnknownDependencyException;
import org.bukkit.plugin.java.JavaPlugin;

import com.rymga.loader.NativeArtifact;
import com.rymga.loader.NativeLoader;
import com.rymga.loader.BundledNativeLibrary;

public final class RymgaPaperBootstrap extends JavaPlugin implements Listener {
    private final AtomicBoolean stopping = new AtomicBoolean();
    private ExecutorService executor;
    private volatile NativeArtifact artifact;
    private volatile Plugin protectedPlugin;

    @Override
    public void onEnable() {
        saveDefaultConfig();
        getServer().getPluginManager().registerEvents(this, this);
        executor = Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "rymga-license");
            thread.setDaemon(true);
            return thread;
        });

        char[] license = PaperCredentials.encode(getConfig().getString("license.user"), getConfig().getString("license.key"));
        if (license == null) {
            deny("license configuration is missing");
            return;
        }
        CompletableFuture.supplyAsync(() -> acquire(license), executor)
                .whenComplete(this::complete);
    }

    private NativeArtifact acquire(char[] license) {
        try {
            var dataDirectory = getDataFolder().toPath();
            NativeLoader.load(BundledNativeLibrary.extract(dataDirectory));
            var caBundle = dataDirectory.resolve("loader-ca.pem");
            return NativeLoader.acquireEmbedded(dataDirectory.resolve("rymga-runtime"),
                    Files.isRegularFile(caBundle) ? caBundle : null, license);
        } catch (IOException exception) {
            return null;
        } finally {
            Arrays.fill(license, '\0');
        }
    }

    private void complete(NativeArtifact value, Throwable failure) {
        if (stopping.get()) {
            if (value != null) value.close();
            return;
        }
        try {
            getServer().getScheduler().runTask(this, () -> finishOnServerThread(value, failure));
        } catch (RuntimeException exception) {
            if (value != null) value.close();
        }
    }

    private void finishOnServerThread(NativeArtifact value, Throwable failure) {
        if (stopping.get()) {
            if (value != null) value.close();
        } else if (failure != null || value == null) {
            deny("license was not accepted");
        } else {
            try {
                load(value);
                getLogger().info("protected plugin enabled");
            } catch (Exception exception) {
                value.close();
                closeArtifact();
                deny("protected plugin could not be loaded");
            }
        }
    }

    private void load(NativeArtifact value)
            throws InvalidPluginException, InvalidDescriptionException, UnknownDependencyException {
        Plugin loaded = getServer().getPluginManager().loadPlugin(value.path().toFile());
        if (loaded == null || loaded.getName().equals(getName())) {
            throw new InvalidPluginException("unexpected protected plugin");
        }
        artifact = value;
        protectedPlugin = loaded;
        getServer().getPluginManager().enablePlugin(loaded);
        if (!loaded.isEnabled()) throw new InvalidPluginException("protected plugin did not enable");
    }

    private void deny(String reason) {
        getLogger().warning(reason + "; plugin disabled");
        getServer().getPluginManager().disablePlugin(this);
    }

    @EventHandler
    public void onPluginDisable(PluginDisableEvent event) {
        if (event.getPlugin() == protectedPlugin) closeArtifact();
    }

    @Override
    public void onDisable() {
        stopping.set(true);
        if (executor != null) executor.shutdownNow();
        Plugin loaded = protectedPlugin;
        if (loaded != null && loaded.isEnabled()) getServer().getPluginManager().disablePlugin(loaded);
        closeArtifact();
    }

    private synchronized void closeArtifact() {
        NativeArtifact value = artifact;
        artifact = null;
        if (value != null) value.close();
    }
}
