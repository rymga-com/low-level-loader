# Protocol v1

`protocol` is a small canonical binary library shared conceptually by Java gateway code and the C client. It
does not perform networking or file I/O.

`SessionRequestCodec` emits the `RLSR` magic, version, timestamp, 32-byte nonce, product, channel, loader
version, installation ID, and license using bounded UTF-8 fields and big-endian integers. The request is
transport-protected by HTTPS but is not signed.

`SessionManifestCodec` emits the `RLSM` magic, version, key ID, original nonce, session ID, issue/expiry times,
product, channel, artifact ID, safe `.jar` filename, exact byte size, SHA-256, and a 64-byte Ed25519 signature.
`unsignedBytes()` is the canonical byte sequence signed by the gateway. The decoder rejects truncation,
oversized fields, malformed UTF-8, invalid identifiers, unsafe filenames, and trailing bytes.

TLS authenticates the channel; the signed manifest authenticates authorization and content metadata. The nonce
binds the response to one request, while timestamps and a short TTL limit replay. Key IDs allow active and next
public keys to coexist during rotation.
