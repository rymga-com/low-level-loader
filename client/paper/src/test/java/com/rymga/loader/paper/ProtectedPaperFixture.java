package com.rymga.loader.paper;

import org.bukkit.plugin.java.JavaPlugin;

public final class ProtectedPaperFixture extends JavaPlugin {
    @Override
    public void onEnable() {
        getLogger().info("rymga protected paper fixture enabled");
    }
}
