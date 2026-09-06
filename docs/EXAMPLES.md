# Examples and fixtures

`examples/application` is a conventional executable JAR used to show that the loader does not require a
runtime API. `examples/plugin-host` is a small host that discovers a plugin from a directory and demonstrates
the generic `preload` path. The host and plugin use ordinary Java classes and `ServiceLoader`.

`examples/fixtures/demo-plugin-rymga.jar.b64` is the deterministic protected artifact used by the HTTPS
end-to-end workflow. It is decoded into a temporary artifact directory; it is not a client dependency or a
production license. `examples/fixtures/rymga-config.toml` records the fixture-generation settings.

These examples intentionally keep the protected JAR conventional. Any JAR accepted by the target JVM or host,
including one produced by an external obfuscator, follows the same delivery path without class-by-class JNI
calls or loader-specific bytecode transformations.
