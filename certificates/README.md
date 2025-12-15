# Certificates used for HTTPS

This project uses a curated CA certificate bundle for TLS/SSL verification on the ESP32.

The bundle is sourced from **curl’s official CA extract** and can optionally be extended with **user-provided custom CA certificates**.

## Source of Trust

- The base CA bundle comes from:
  [https://curl.se/docs/caextract.html](https://curl.se/docs/caextract.html)
- `cacert.pem` is downloaded directly from curl.se
- A SHA-256 checksum is computed locally and compared against curl’s published checksum
- All certificates are parsed and validated using `cryptography`
- Any parsing error, mismatch, or invalid certificate **aborts generation**

> ⚠️ **Trust warning**
> The PEM file and its checksum are fetched from the same domain.
> If curl.se were compromised or TLS trust failed, both could be malicious but internally consistent.
> **Manual verification against curl’s website is therefore mandatory.**

## Updating the Bundle

### 1. Generate

Run:

```sh
python3 gen_crt_bundle.py
```

The script will:

- Download `cacert.pem`
- Compute its SHA-256 hash
- Download curl’s published checksum
- Verify the PEM matches the computed checksum
- Optionally include custom certificates (see below)
- Abort on any error or inconsistency

### 2. **Mandatory Manual Verification**

⚠️ **Do not commit anything before completing the steps below.**

1. Open: [https://curl.se/docs/caextract.html](https://curl.se/docs/caextract.html)
2. Locate the latest published SHA-256 checksum
3. Manually compare it against:

```sh
sha256sum cacert.pem
```

Both values **must match exactly**.

If they do not:

- Do not commit
- Treat the bundle as untrusted

> Automatic checks ensure internal consistency.
> Manual verification establishes the external trust boundary.

### 3. Commit

After successful manual verification, commit:

- `x509_crt_bundle`
- `cacert.pem`
- `cacert.pem.sha256`

❌ **Never commit anything under `custom_certs/`.**

## Custom Certificates (Optional)

Self-hosted setups may require trusting additional Certificate Authorities (e.g. internal PKI or private reverse proxies).

Custom CA certificates can be added locally and merged into the bundle.

### How to add

Place certificates in:

```text
custom_certs/
```

Only files ending in `.pem` are considered.

### Requirements

Each custom certificate file must contain exactly one PEM certificate block and nothing else.

The script will parse and validate them as CA certificates and reject anything invalid or duplicated.

> ⚠️ Adding custom CAs expands the ESP32’s trust boundary.
> Only add certificates you fully trust and control.

If `custom_certs/` does not exist or doesn't contain any .pem certificates, only curl’s CA bundle is used.

## Trust Model Summary

- Automatic verification protects against corruption and mismatched downloads
- Manual verification anchors trust outside the update mechanism
- Custom certificates explicitly extend trust and are opt-in only
- The generated bundle is deterministic for a given input set

## References

- [curl - CA Extract](https://curl.se/docs/caextract.html)
- [Espressif ESP-IDF certificate bundle documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_crt_bundle.html)
