# Architecture

## Components

```text
External license provider  <-- server-to-server HTTPS -->  Quarkus gateway
                                                              | signed manifest
                                                              v
Client native core (C) -- HTTPS session and artifact download --> private JAR
        |                                      |
        +-- native launcher ------------------+--> java -jar / plugin host
        +-- JNI bridge --> Java application bootstrap or Paper adapter
```

The provider is replaceable. The gateway owns provider credentials and the Ed25519 private key. The client
contains only a trusted public key and product policy compiled into the native library.

## Acquisition

1. The client validates local inputs and creates a cryptographic nonce.
2. C sends a bounded `SessionRequest` over HTTPS to `POST /v1/session`.
3. The gateway validates the request timestamp and asks the external provider whether the license is valid.
4. The gateway resolves a configured artifact snapshot and signs a `SessionManifest` containing the nonce,
   identity, expiry, size, and SHA-256.
5. C verifies the signature, nonce, identity, time window, filename, and limits.
6. C downloads exactly the authorized byte range over HTTPS into a private temporary file and hashes bytes as
   they arrive.
7. Only an exact size and hash match is atomically published. A live artifact handle owns the file.

No execution occurs before every check succeeds. A failed license, unavailable provider, invalid signature,
bad TLS, truncated response, or integrity mismatch fails closed.

## Application bundle

`client-java:applicationBundle` produces one executable JAR per platform. It contains Java bootstrap classes,
the platform JNI library, and third-party license notices. It does not contain product secrets.

`ApplicationBootstrap` locates its own directory, creates a strict `config.yml` on first run, extracts JNI by
platform and SHA-256, and calls `NativeLoader.acquireEmbedded`. It then starts the same Java executable in a
child process with `-jar <verified-artifact>`, passes application arguments unchanged, inherits standard I/O,
waits for completion, and returns the child's exit code. JNI is used only for acquisition; class loading and
execution are entirely standard JVM behavior.

## Paper bundle

Paper already owns a running JVM and cannot be wrapped by a parent process. The bootstrap reads its license
configuration, performs extraction and native acquisition on a daemon executor, then returns to the Paper
main thread and calls the public `PluginManager.loadPlugin(File)` and `enablePlugin(Plugin)` methods. A failure
disables only the bootstrap; the server continues loading other plugins. The protected artifact remains a
normal Paper plugin with its own descriptor.

## Generic plugins and launcher

The native `preload` launcher materializes a verified JAR in a private plugin directory, starts the host
normally, and removes only files it owns. It is suitable for hosts that discover plugins from directories or
`ServiceLoader`. Hosts with a proprietary dynamic API need a separate adapter. The native `run` launcher is
kept for deployments that need explicit JVM arguments, stdin, signal forwarding, or host control.

## Security limits

TLS provides transport confidentiality and server authentication; Ed25519 authenticates the gateway's
authorization and artifact metadata; SHA-256 binds the downloaded bytes. The nonce and short expiry prevent
cross-request replay. None of these mechanisms creates an enclave. A user controlling an authorized machine
can patch the client, inspect memory, copy the temporary JAR, or dump loaded classes. The system protects
authorized delivery, not perpetual execution secrecy.

## Performance

The entire JAR is downloaded once. Class loading remains lazy inside the normal JVM or host. There are no JNI
calls per class, method, resource, reflection operation, or `ServiceLoader` entry. Steady-state execution has
no loader overhead; startup cost is network latency, hashing, file I/O, and (for application bundles) one
lightweight bootstrap JVM plus the protected JVM.
