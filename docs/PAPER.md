# Paper integration

Build the native JNI library for the target platform, then run:

```bash
./gradlew :paper-plugin:paperBundle \
  -PpaperNativeLibrary="$PWD/build/native/librymga_loader_jni.so" \
  -PpaperNativePlatform=linux-x86_64
```

Copy the resulting `rymga-paper-loader-<platform>.jar` to `plugins/`. Paper creates the plugin data folder
and `config.yml` on the first server start:

```yaml
license:
  user: ""
  key: "LICENSE"
```

Set the license and restart. No JNI file, libcurl, libsodium, OpenSSL, or zlib is copied manually. The bundle
contains the native library and third-party notices. An optional `loader-ca.pem` in the plugin data folder
selects a private CA bundle.

The bootstrap never waits for network I/O on Paper's main thread. It loads and enables the protected JAR
through Paper's public plugin manager only after C has verified the signed manifest and exact artifact hash.
Missing or invalid credentials, gateway errors, JNI failures, and incompatible plugin descriptors disable only
the bootstrap; the server and other plugins continue.
