# Certificates used for HTTPS

These certificates are sourced from [curl’s CA bundle](https://curl.se/docs/caextract.html) and are used for TLS/SSL verification on the ESP32.

The bundle is generated as follows:

- Downloaded directly from curl.se
- The downloaded `cacert.pem` is:

  - Hashed locally using SHA-256
  - Verified against the downloaded `cacert.pem.sha256`

- **The user must manually validate the downloaded file and checksum against curl’s official published SHA-256**
- Parsed using `cryptography`, aborting on any invalid certificate

> **Important trust note:**
> The PEM file and the `.sha256` file are fetched from the same domain.
> If curl.se were compromised or a TLS trust failure occurred during update, **both files could be malicious but internally consistent**.
> This is why **manual verification against curl’s website is mandatory**.

---

## How to update

### 1. Generate the bundle

Run:

```sh
python gen_crt_bundle.py
```

The script will:

- Download `cacert.pem` from curl.se
- Save it locally as `cacert.pem`
- Compute and print its SHA-256 hash
- Save the computed hash locally as `cacert.pem.sha256`
- Download curl’s published `cacert.pem.sha256`
- Automatically verify that:

  - The downloaded PEM matches the downloaded checksum

- Abort immediately on any mismatch, parsing error, or invalid certificate

---

### 2. **MANDATORY: SHA-256 validation (two-step trust check)**

⚠️ **This step is REQUIRED before committing any changes.**

#### Step A — Validate file ↔ checksum

Verify that the downloaded file matches the downloaded checksum:

```sh
sha256sum cacert.pem
cat cacert.pem.sha256
```

✅ The hash printed by `sha256sum` **must exactly match** the contents of `cacert.pem.sha256`.

❌ If it does not match:

- Treat both files as untrusted
- Do **not** commit anything
- Investigate the failure before proceeding

#### Step B — Validate checksum ↔ curl’s official publication

1. Open curl’s official CA extract page:
   [https://curl.se/docs/caextract.html](https://curl.se/docs/caextract.html)
2. Locate the published SHA-256 checksum for `cacert.pem`
3. **Manually compare** it against:

   - The hash printed by the script
   - The contents of the local `cacert.pem.sha256`

✅ **All three values must match exactly**.

❌ If they do **not** match:

- **Do not commit the bundle**
- Treat the output as untrusted
- Investigate the discrepancy before proceeding

> Automatic checksum verification protects against corruption and mismatched downloads.
> **Manual verification establishes the external trust boundary and is the final authority.**

---

### 3. Commit the updated bundle

Only after **both** validation steps succeed, commit the newly generated artifacts:

- `x509_crt_bundle`
- `cacert.pem`
- `cacert.pem.sha256`

---

## Notes on trust

- Automatic SHA-256 verification ensures internal consistency of downloaded files.
- Validating `cacert.pem` against `cacert.pem.sha256` ensures file integrity.
- Manual verification against curl’s official publication establishes external trust.
- The generated `x509_crt_bundle` is deterministic for a given `cacert.pem`.

---

## References

- [curl - CA Extract](https://curl.se/docs/caextract.html)
- [Espressif ESP-IDF certificate bundle documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_crt_bundle.html)
