# Documentation

This directory is the public English documentation for the loader.

- [`ARCHITECTURE.md`](ARCHITECTURE.md): trust boundaries, request flow, application and plugin execution,
  security limits, and performance model.
- [`BUILD.md`](BUILD.md): development prerequisites, pinned vcpkg builds, platform bundles, and CI checks.
- [`CODE_REFERENCE.md`](CODE_REFERENCE.md): responsibilities of every Java, C, gateway, protocol, and build
  component.
- [`PROTOCOL.md`](PROTOCOL.md): binary request and signed-manifest contract shared by Java and C.
- [`GATEWAY.md`](GATEWAY.md): Quarkus configuration, external license providers, artifact snapshots, and
  deployment rules.
- [`APPLICATION.md`](APPLICATION.md): consumer installation and the direct `java -jar` application flow.
- [`PAPER.md`](PAPER.md): Paper installation, configuration, lifecycle, and dynamic plugin loading.
- [`EXAMPLES.md`](EXAMPLES.md): application, plugin-host, and protected-fixture behavior.

The root [`README.md`](../README.md) is the short entry point. These documents describe the implemented
behavior; they do not promise protection against an administrator controlling an authorized machine.
