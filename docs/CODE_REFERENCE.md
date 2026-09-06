# Code reference

## Java client

- `NativeLoader` is the single JNI boundary: configurable `acquire`, build-policy `acquireEmbedded`, explicit
  library `load`, and handle release. License arrays are wiped in `finally`.
- `NativeRequest` and `NativeTrustedKey` validate immutable configurable requests and defensive key copies.
- `NativeArtifact` owns the native handle and keeps the artifact alive until `close`.
- `BundledNativeLibrary` selects Linux/Windows/macOS plus x86_64/aarch64, extracts at most 64 MiB, hashes,
  atomically installs, caches by SHA-256, and repairs altered files.
- `ApplicationConfig` accepts only the bounded `license` YAML subset and rejects symlinks, duplicate fields,
  unknown fields, invalid UTF-8, and malformed quoting.
- `ApplicationBootstrap` creates first-run configuration, acquires embedded policy, starts the protected JAR,
  and propagates its exit code.

## Paper

`RymgaPaperBootstrap` performs asynchronous acquisition, switches back to the Paper scheduler, loads the
protected plugin via public APIs, and releases the artifact on plugin/server shutdown. `PaperCredentials`
converts optional user plus required key to a wiped character array. The protected plugin retains its own
`plugin.yml`.

## Native C

- `src/rymga_loader.c` validates all bounds, creates nonces, performs HTTPS, decodes and verifies manifests,
  downloads and hashes the artifact, and publishes only complete files.
- `src/rymga_platform_posix.c` and `src/rymga_platform_windows.c` implement private directories, safe temp
  files, identity checks, cleanup markers, and conservative plugin cleanup.
- `jni/rymga_loader_jni.c` converts strict UTF-16/UTF-8 values, calls the same core, and exposes only acquire,
  path, and release.
- `launcher/rymga_loader.c` and `launcher/rymga_loader_windows.c` implement explicit `run` and `preload`
  modes, stdin/terminal handling, process lifecycle, Unicode, and signal propagation.
- `CMakeLists.txt` selects platform sources, enforces warnings, sanitizers/fuzzing, embedded policy validation,
  static dependency mode, license notices, and CPack output.

## Gateway and protocol

`GatewayResource` is the HTTP boundary. `SessionService` orchestrates provider validation, catalog lookup, and
manifest signing. `LicenseValidator` supports mock and HTTP providers. `ArtifactCatalog` snapshots and hashes
one file handle. `SigningKeys` loads and cross-checks Ed25519 keys. `protocol` contains bounded `Wire` readers,
writers, request/manifest models, codecs, and Java Ed25519 verification.

## Tests

Java self-tests cover configuration, extraction, cache repair, protocol vectors, gateway validation, and JNI
integration. Native CTest covers temporary-file and platform guarantees. CI adds sanitizers, fuzzing, HTTPS
provider/gateway end-to-end tests, Paper and application bundle extraction, JVM execution, Unicode paths, and
static dependency audits.
