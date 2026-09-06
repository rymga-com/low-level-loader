# Build and distribution

## Complete local verification

Run the repository-wide entry point from the project root:

```bash
./scripts/build-and-verify.sh
```

It detects the current operating system and architecture, runs every Gradle check, builds the Quarkus
gateway, prepares the pinned vcpkg revision, builds and tests the self-contained native client, generates and
loads both application and Paper bundles, creates CPack archives, and rejects unexpected dynamic libraries.
The generated artifact paths are printed at the end.

The script validates only the current native platform. GitHub Actions invokes the same script on Linux,
macOS, and Windows workers and then runs each platform's HTTPS end-to-end scenarios. A successful local run
does not replace the three-platform CI matrix.

The embedded values default to the test product. A product build can override them without editing source:

```bash
RYMGA_ENDPOINT=https://loader.example.com \
RYMGA_PRODUCT=my-product \
RYMGA_KEY_ID=main-2026 \
RYMGA_PUBLIC_KEY_HEX=<64-hex> \
./scripts/build-and-verify.sh
```

`RYMGA_CHANNEL`, `RYMGA_LOADER_VERSION`, `RYMGA_INSTALLATION_ID`, `RYMGA_VCPKG_ROOT`, and
`RYMGA_BUILD_DIR` are also supported. None of these values is a server secret; the private signing key and
license-provider credential must remain outside client builds.

## Local development

Install JDK 21, CMake 3.20+, a C11 compiler, and development packages for curl and libsodium. Then run:

```bash
./gradlew check
cmake -S client/native -B build/native
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

The ordinary build uses system development libraries and is intended for fast tests. It is not the release
distribution.

## Self-contained native build

The release build uses the baseline pinned in `client/native/vcpkg.json`:

```bash
git clone https://github.com/microsoft/vcpkg.git tools/vcpkg
git -C tools/vcpkg checkout 04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4
tools/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export RYMGA_VCPKG_ROOT="$PWD/tools/vcpkg"
cmake -S client/native -B build/native-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$RYMGA_VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DRYMGA_SELF_CONTAINED=ON
cmake --build build/native-release --parallel
cpack --config build/native-release/CPackConfig.cmake
```

Linux requires autoconf, autoconf-archive, automake, and libtool when vcpkg builds libsodium. macOS uses the
corresponding Homebrew packages. Windows uses the `x64-mingw-static` triplet and `bootstrap-vcpkg.bat`.
Self-contained mode rejects shared libcurl/libsodium, statically links curl, libsodium, TLS, and zlib, and
copies their license notices into the package.

## Application bundle

Configure the native build with product policy and trusted public key:

```bash
cmake -S client/native -B build/native-application \
  -DCMAKE_TOOLCHAIN_FILE="$RYMGA_VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux -DRYMGA_SELF_CONTAINED=ON \
  -DRYMGA_EMBEDDED_CONFIG=ON \
  -DRYMGA_EMBEDDED_ENDPOINT=https://loader.example.com \
  -DRYMGA_EMBEDDED_PRODUCT=my-application \
  -DRYMGA_EMBEDDED_CHANNEL=stable \
  -DRYMGA_EMBEDDED_LOADER_VERSION=1.0.0 \
  -DRYMGA_EMBEDDED_INSTALLATION_ID=application \
  -DRYMGA_EMBEDDED_KEY_ID=main-2026 \
  -DRYMGA_EMBEDDED_PUBLIC_KEY_HEX=<64-hex>
cmake --build build/native-application --parallel
./gradlew :client-java:applicationBundle \
  -PapplicationNativeLibrary="$PWD/build/native-application/librymga_loader_jni.so" \
  -PapplicationNativePlatform=linux-x86_64
```

The resulting `rymga-application-loader-linux-x86_64.jar` is the consumer artifact. Build separately for
`linux-{x86_64,aarch64}`, `windows-{x86_64,aarch64}`, and `macos-{x86_64,aarch64}`. The Paper task follows
the same native build and uses `:paper-plugin:paperBundle`.

## Checks and CI

CI runs Gradle checks, CTest, ASan/UBSan, manifest fuzzing, HTTPS end-to-end tests, JNI `-Xcheck:jni`, native
dependency audits, and application/Paper bundle extraction. Linux and Windows audits reject non-system shared
libraries; macOS rejects non-system install names. CI is the final confirmation for native runners not
available on the development host.
