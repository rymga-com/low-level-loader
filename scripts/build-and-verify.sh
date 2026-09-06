#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"

for required_command in git cmake java jar grep; do
    command -v "$required_command" >/dev/null || { printf 'Missing required command: %s\n' "$required_command" >&2; exit 1; }
done

system_name="$(uname -s)"
architecture="$(uname -m)"
generator="Unix Makefiles"
chainload_argument=

case "$system_name" in
    Linux)
        command -v readelf >/dev/null || { printf 'Missing required command: readelf\n' >&2; exit 1; }
        case "$architecture" in
            x86_64|amd64) platform=linux-x86_64; triplet=x64-linux ;;
            aarch64|arm64) platform=linux-aarch64; triplet=arm64-linux ;;
            *) printf 'Unsupported architecture: %s\n' "$architecture" >&2; exit 1 ;;
        esac
        native_filename=librymga_loader_jni.so
        bootstrap=sh
        ;;
    Darwin)
        command -v otool >/dev/null || { printf 'Missing required command: otool\n' >&2; exit 1; }
        case "$architecture" in
            x86_64|amd64) platform=macos-x86_64; triplet=x64-osx ;;
            aarch64|arm64) platform=macos-aarch64; triplet=arm64-osx ;;
            *) printf 'Unsupported architecture: %s\n' "$architecture" >&2; exit 1 ;;
        esac
        native_filename=librymga_loader_jni.dylib
        bootstrap=sh
        ;;
    MINGW*|MSYS*|CYGWIN*)
        command -v objdump >/dev/null || { printf 'Missing required command: objdump\n' >&2; exit 1; }
        command -v cygpath >/dev/null || { printf 'Missing required command: cygpath\n' >&2; exit 1; }
        case "$architecture" in
            x86_64|amd64) platform=windows-x86_64; triplet=x64-mingw-static ;;
            aarch64|arm64) platform=windows-aarch64; triplet=arm64-mingw-static ;;
            *) printf 'Unsupported architecture: %s\n' "$architecture" >&2; exit 1 ;;
        esac
        native_filename=rymga_loader_jni.dll
        bootstrap=bat
        generator=Ninja
        ;;
    *)
        printf 'Unsupported operating system: %s\n' "$system_name" >&2
        exit 1
        ;;
esac

vcpkg_baseline=04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4
vcpkg_root="${RYMGA_VCPKG_ROOT:-$project_root/build/vcpkg}"
native_build="${RYMGA_BUILD_DIR:-$project_root/build/verify-$platform}"

./gradlew --no-daemon check :gateway:quarkusBuild

if [ ! -d "$vcpkg_root/.git" ]; then
    mkdir -p "$vcpkg_root"
    git -C "$vcpkg_root" init
fi
if ! git -C "$vcpkg_root" remote get-url origin >/dev/null 2>&1; then
    git -C "$vcpkg_root" remote add origin https://github.com/microsoft/vcpkg.git
fi
git -C "$vcpkg_root" fetch --depth 1 origin "$vcpkg_baseline"
git -C "$vcpkg_root" checkout --detach FETCH_HEAD

if [ "$bootstrap" = bat ]; then
    cmd.exe /c "$(cygpath -w "$vcpkg_root/bootstrap-vcpkg.bat")" -disableMetrics
    toolchain="$(cygpath -m "$vcpkg_root/scripts/buildsystems/vcpkg.cmake")"
    chainload="$(cygpath -m "$vcpkg_root/scripts/toolchains/mingw.cmake")"
    chainload_argument="-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$chainload"
else
    "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
    toolchain="$vcpkg_root/scripts/buildsystems/vcpkg.cmake"
fi

cmake -S client/native -B "$native_build" -G "$generator" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    ${chainload_argument:+"$chainload_argument"} \
    -DVCPKG_TARGET_TRIPLET="$triplet" \
    -DRYMGA_SELF_CONTAINED=ON \
    -DRYMGA_EMBEDDED_CONFIG=ON \
    -DRYMGA_EMBEDDED_ENDPOINT="${RYMGA_ENDPOINT:-https://localhost:8443}" \
    -DRYMGA_EMBEDDED_PRODUCT="${RYMGA_PRODUCT:-demo-plugin}" \
    -DRYMGA_EMBEDDED_CHANNEL="${RYMGA_CHANNEL:-stable}" \
    -DRYMGA_EMBEDDED_LOADER_VERSION="${RYMGA_LOADER_VERSION:-1.0.0}" \
    -DRYMGA_EMBEDDED_INSTALLATION_ID="${RYMGA_INSTALLATION_ID:-local-verify}" \
    -DRYMGA_EMBEDDED_KEY_ID="${RYMGA_KEY_ID:-dev-2026}" \
    -DRYMGA_EMBEDDED_PUBLIC_KEY_HEX="${RYMGA_PUBLIC_KEY_HEX:-d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a}"

cmake --build "$native_build" --parallel
ctest --test-dir "$native_build" --output-on-failure

native_library="$native_build/$native_filename"
gradle_native_library="$native_library"
if [ "$bootstrap" = bat ]; then gradle_native_library="$(cygpath -w "$native_library")"; fi

./gradlew --no-daemon :client-java:applicationBundleSelfTest :paper-plugin:paperBundleSelfTest \
    -PapplicationNativeLibrary="$gradle_native_library" \
    -PapplicationNativePlatform="$platform" \
    -PpaperNativeLibrary="$gradle_native_library" \
    -PpaperNativePlatform="$platform"

cmake --build "$native_build" --target package --parallel

case "$system_name" in
    Linux)
        unexpected="$(for binary in "$native_library" "$native_build/rymga-loader"; do readelf -d "$binary" | grep 'Shared library:' | grep -Ev 'Shared library: \[lib(c|m|dl|pthread|rt)\.so' || true; done)"
        ;;
    Darwin)
        unexpected="$(for binary in "$native_library" "$native_build/rymga-loader"; do otool -L "$binary" | tail -n +2 | awk '{print $1}' | grep -Ev '^(/usr/lib/|/System/Library/)' || true; done)"
        ;;
    *)
        unexpected="$(for binary in "$native_library" "$native_build/rymga-loader.exe"; do objdump -p "$binary" | awk '/DLL Name:/{print tolower($3)}' | grep -Ev '^(advapi32|bcrypt|crypt32|iphlpapi|kernel32|msvcrt|secur32|ws2_32)\.dll$' || true; done)"
        ;;
esac

if [ -n "$unexpected" ]; then
    printf 'Unexpected dynamic dependencies:\n%s\n' "$unexpected" >&2
    exit 1
fi

application_bundle="$project_root/client/java/build/libs/rymga-application-loader-$platform.jar"
paper_bundle="$project_root/client/paper/build/libs/rymga-paper-loader-$platform.jar"
test -f "$application_bundle"
test -f "$paper_bundle"
jar tf "$application_bundle" | grep -Fx "META-INF/rymga/native/$platform/$native_filename" >/dev/null
jar tf "$paper_bundle" | grep -Fx "META-INF/rymga/native/$platform/$native_filename" >/dev/null

printf 'Build and verification succeeded for %s.\n' "$platform"
printf 'Application bundle: %s\n' "$application_bundle"
printf 'Paper bundle: %s\n' "$paper_bundle"
printf 'Native packages: %s/packages\n' "$native_build"
