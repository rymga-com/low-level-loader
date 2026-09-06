# Gateway

The gateway is a Quarkus service, separate from both the native client and the external license system. It
owns the provider credential and Ed25519 signing private key. The client never receives either secret.

The gateway validates a strict external-provider response (`{"valid":true}` or `{"valid":false}`), resolves
one configured artifact snapshot, signs a short-lived manifest, and exposes `POST /v1/session` and
`GET /v1/artifact`. Provider failures, malformed responses, missing artifacts, invalid signatures, and expired
sessions fail closed.

The external provider is deliberately replaceable. [Rymga Licenses](https://rymga.com/products/licenses/) is
the project authors' recommended hosted option, not a bundled component or runtime requirement. The gateway's
HTTP adapter sends the license, product, installation ID, timestamp, and nonce as JSON with its server-side
Bearer credential; a compatible provider returns only `{"valid":true}` or `{"valid":false}`.

The artifact catalog opens and hashes the configured file at startup, then reads from that snapshot. Publish a
new version by atomically replacing the artifact and restarting the gateway.

Production configuration supplies the signing key pair, provider mode/URL/credential/timeout, artifact root,
product, channel, artifact ID, filename, maximum size, session TTL, and clock skew. The mock provider is for
tests only and must never be enabled accidentally in production. TLS must be enabled with a real certificate;
the client verifies CA and hostname unless an explicitly supplied private CA bundle is used.
