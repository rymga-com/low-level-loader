# Application bundle

The application bundle is the zero-manual-step consumer distribution. It requires only Java 21 or newer and
the bundle matching the operating system and architecture.

```text
java -jar rymga-application-loader-linux-x86_64.jar [application arguments]
```

On the first run, `config.yml` is created beside the bundle and the process exits with status `2`:

```yaml
license:
  user: ""
  key: "LICENSE"
```

Set `key` and the optional `user`, then run the same command again. The bootstrap performs the complete native
session, starts the downloaded JAR in a normal child JVM, forwards its standard I/O and arguments, and returns
its exit status. A failed license or unavailable gateway never starts the artifact.

The endpoint, product, channel, and trusted public key are build-time native policy. They are not secrets and
are not editable in the consumer YAML. Put `loader-ca.pem` beside the bundle only when the gateway uses a
private certificate authority; otherwise the operating system CA store is used. `JAVA_TOOL_OPTIONS` is the
standard way to apply JVM options to both bootstrap and child JVMs.

The native library is extracted to `.rymga-loader/native/<sha256>/` with private permissions and repaired if
corrupted. The protected JAR exists only in the loader-managed temporary directory while the child runs.
