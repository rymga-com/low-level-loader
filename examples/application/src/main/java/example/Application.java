package example;

public final class Application {
    private Application() {
    }

    public static void main(String[] args) {
        System.out.println("application loaded: " + String.join(",", args));
    }
}
