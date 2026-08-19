# Certificates used for HTTPS

This project compiles a curated CA trust store into the firmware for TLS/SSL verification on
the ESP32. The build packs `cacert-merged.pem` into the flash image via ESP-IDF's
certificate-bundle step, and the ESP32 verifies every TLS server (gateway WebSocket, HTTP/OTA)
against it.

`certificates/cert_bundle.py` manages the store. It has two subcommands:

```sh
python3 cert_bundle.py generate   # (default) rebuild cacert-curl.pem + cacert-merged.pem
python3 cert_bundle.py audit      # audit expiry + live-endpoint relevance (used by CI)
```

## The two files

| File | What it is | Packed into firmware? |
|---|---|---|
| `cacert-curl.pem` | **Verbatim** curl/Mozilla CA extract, exactly as downloaded. Its `sha256sum` matches curl's published hash. | No — reference/base only |
| `cacert-merged.pem` | Generated: the base **plus** `pinned_certs/*.pem` (plus any local `custom_certs/*.pem`), de-duplicated, with an OpenShock header banner instead of curl's. | **Yes** |

Both are tracked and committed. `cacert-merged.pem` is what actually ships; `cacert-curl.pem`
exists so the base can be independently checksum-verified against curl.

### Why pinned roots exist

esp_crt_bundle does **not** do certificate-path building. It trusts the chain exactly as a
server presents it and looks up the **issuer of the topmost presented cert** among the trusted
roots. If a server sends a legacy cross-signed root at the top of its chain, the anchor it
"requires" is that cross-signer — which a modern (Mozilla-derived) store may no longer include.

Concretely: `api.openshock.app` presents `leaf → GTS WE1 → GTS Root R4`, where R4 is the
**cross-signed** copy issued by `GlobalSign Root CA` (R1). Mozilla/curl retired R1, so without
it esp_crt_bundle reports *"No matching trusted root certificate found"*. `pinned_certs/` carries
R1 so that chain verifies. (The cleaner long-term fix is trimming the server chain so it
validates against `GTS Root R4`, which every store already ships — see **Auditing** below.)

## Source of Trust

- The base CA bundle comes from: [https://curl.se/docs/caextract.html](https://curl.se/docs/caextract.html)
- `cacert-curl.pem` is the base PEM downloaded from curl.se; its SHA-256 is compared against
  curl's published checksum
- Every certificate (base, pinned, custom) is parsed and validated with `cryptography`
- Pinned/custom certs must be CA certificates (BasicConstraints CA=TRUE)
- The merged output is **de-duplicated** by certificate DER, and duplicate subject names are
  rejected (they would break esp_crt_bundle's subject-name lookup)
- Any parsing error, checksum mismatch, or an already-expired pinned root **aborts generation**

> ⚠️ **Trust warning**
> The base PEM and its checksum are fetched from the same domain. If curl.se were compromised
> or TLS trust failed, both could be malicious but internally consistent.
> **Manual verification against curl's website is therefore mandatory.**

## Updating the Bundle

### 1. Generate

```sh
python3 cert_bundle.py generate
```

The script will:

- Download the curl base PEM and compute its SHA-256
- Download curl's published checksum and verify the download matches it
- Parse/validate the base + `pinned_certs/*.pem` (+ any `custom_certs/*.pem`)
- Reject non-CA pins, duplicate subjects, and already-expired pins; de-dup identical certs
- Write `cacert-curl.pem` (verbatim base) and `cacert-merged.pem` (base + pinned + custom)

### 2. **Mandatory Manual Verification**

⚠️ **Do not commit anything before completing the steps below.**

`cacert-curl.pem` is the verbatim curl extract, so verify it directly:

1. Open: [https://curl.se/docs/caextract.html](https://curl.se/docs/caextract.html)
2. Locate the latest published SHA-256 checksum
3. Compare it against:

```sh
sha256sum cacert-curl.pem
```

Both values **must match exactly**. If they do not: do not commit; treat the bundle as untrusted.

### 3. Commit

After successful manual verification, commit:

- `cacert-curl.pem`
- `cacert-merged.pem`
- any change under `pinned_certs/`

`cacert-merged.pem` is the packed artifact. The script does **not** emit a packed
`x509_crt_bundle` — the firmware build converts and packs `cacert-merged.pem` into flash itself
via ESP-IDF's certificate-bundle step (`CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH` in
`sdkconfig.defaults`).

> ⚠️ **Build gotcha:** the ESP-IDF cert-bundle step does not always detect a changed
> `cacert-merged.pem`. After regenerating, delete `.pio/build/<env>/x509_crt_bundle*` (or do a
> clean build) or the firmware may silently ship the previous bundle.

## Pinned Certificates (`pinned_certs/`)

Roots we intentionally ship on top of the Mozilla base. **Tracked and committed.** Each file
must contain exactly one PEM certificate block (an optional leading label/comment line, as in
curl's bundle, is allowed) and must be a CA certificate.

Before adding a pinned root, fingerprint-verify it out-of-band against at least one independent
source (e.g. the CA's own repository plus Cloudflare's `cfssl_trust`), then drop the PEM into
`pinned_certs/` and run `cert_bundle.py generate`.

### Auditing

`cert_bundle.py audit` (run weekly by the `pinned-cert-audit` CI workflow):

- **Expiry** — checks every cert in `cacert-merged.pem`. A still-needed pinned root that is
  expired or within the warn window (`PINNED_CERT_EXPIRY_WARN_DAYS`, default 90) fails the job
  so we rotate it in time.
- **Relevance** — probes `api.openshock.app` / `api.openshock.dev` and works out which anchor
  each requires. A pinned root that **no** reachable endpoint chains through anymore is dead
  weight; the workflow opens a PR that deletes it and regenerates the bundle. (Fail-safe: if a
  probe is unreachable, nothing is retired.)

This is what makes the pins self-cleaning: once the servers stop sending a legacy cross-signed
root, the corresponding pin is automatically proposed for removal.

## Custom Certificates (Optional)

Self-hosted setups may need to trust additional CAs (internal PKI, private reverse proxies).
Drop them in `custom_certs/` locally — this directory is **git-ignored** and never committed.

- Only `*.pem` files are considered; each must contain exactly one CA certificate (an optional
  leading label/comment line is allowed, but no trailing data or private keys).
- The script validates them and rejects anything invalid or duplicated.

> ⚠️ Adding custom CAs expands the ESP32's trust boundary. Only add certificates you fully
> trust and control.

> **Note:** custom-cert support is currently **dormant** in shipped builds — the committed
> `cacert-merged.pem` is generated without any `custom_certs/`, so custom certs only reach the
> trust store in a local rebuild.

## Trust Model Summary

- Automatic verification protects against corruption and mismatched downloads
- Manual verification (`sha256sum cacert-curl.pem` vs curl) anchors trust outside the update mechanism
- Pinned roots are explicit, fingerprint-verified additions, and are expiry/relevance-audited
- The merged store is de-duplicated and cannot contain the same certificate twice
- Custom certificates explicitly extend trust and are opt-in, local-only
- `cacert-merged.pem` = verified curl base + `pinned_certs/` (+ local `custom_certs/`)

## References

- [curl - CA Extract](https://curl.se/docs/caextract.html)
- [Espressif ESP-IDF certificate bundle documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_crt_bundle.html)
