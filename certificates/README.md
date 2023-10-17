# Certificates used for HTTPS

These certificates are fetched from [CURL's website](https://curl.se/docs/caextract.html) and are used for HTTPS connections.

## How to update

1. Download the latest version of the certificate bundle along with its sha256 from [CURL's website](https://curl.se/docs/caextract.html).
2. Replace the `cacrt_all.pem` file in this directory with the new one.
3. Run `gen_crt_bundle.py` to generate the new `x509_crt_bundle` file.
4. Move the new `x509_crt_bundle` file to the `data/cert` directory.
5. Commit the changes.

## References

- [CURL's website](https://curl.se/docs/caextract.html)
- [Espressif's documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_crt_bundle.html)
- [WiFiClientSecure Documentation](https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFiClientSecure#using-a-bundle-of-root-certificate-authority-certificates)
