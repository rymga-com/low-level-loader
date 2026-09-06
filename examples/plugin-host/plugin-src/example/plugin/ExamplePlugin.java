package example.plugin;

public final class ExamplePlugin implements Plugin {
    @Override
    public void start() {
        System.out.println("plugin loaded normally");
    }
}
