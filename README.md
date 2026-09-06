# Rymga Low-Level Loader

Open-source delivery loader for Java applications and plugins. A Quarkus gateway asks an external license
provider for authorization, signs a short-lived manifest, and serves the exact protected JAR. The native
client performs HTTPS, signature verification, downloading, hashing, and private temporary-file handling.

## Consumer quick start

Use the application bundle built for the target operating system:

```text
java -jar rymga-application-loader-<platform>.jar
```

The first run creates `config.yml` beside the bundle and exits. Set the required license and optional user:

```yaml
license:
  user: ""
  key: "YOUR-LICENSE"
```

Run the same command again. The bootstrap extracts its bundled JNI library, performs one native acquisition,
and starts the protected artifact with the normal Java `-jar` mechanism. No native packages or separate JNI
files are installed. Paper users install the matching Paper bundle in `plugins/`, set the generated
`config.yml`, and restart the server.

## Repository

| Path | Purpose |
|---|---|
| `client/native` | C core, JNI bridge, launchers, platform file handling |
| `client/java` | Java API and executable application bundle |
| `client/paper` | Asynchronous Paper bootstrap and bundle |
| `gateway` | Reference Quarkus gateway and external-provider adapter |
| `protocol` | Canonical Java/C wire-format contract |
| `examples` | Application, plugin-host, and protected fixtures |
| `docs` | English architecture, build, protocol, gateway, and code reference |

## Developer checks

```bash
./scripts/build-and-verify.sh
```

Production bundles use the pinned `client/native/vcpkg.json` manifest and `RYMGA_SELF_CONTAINED=ON`; see
[`docs/BUILD.md`](docs/BUILD.md). The CI workflow builds and audits Linux, macOS, and Windows artifacts.

## Security boundary

The gateway is the authorization boundary. The client verifies TLS, hostname, nonce, timestamps, Ed25519,
artifact identity, exact size, and SHA-256 before exposing a path. The loader is not DRM: an administrator of
an authorized machine can copy or debug the JAR while it runs. Private signing keys and license-provider
credentials belong only on the gateway.

Read [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) before changing the flow and
[`docs/CODE_REFERENCE.md`](docs/CODE_REFERENCE.md) before changing a class or native module.
