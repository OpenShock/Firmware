# Certificates used for HTTPS

These certificates are fetched from [CURL's website](https://curl.se/docs/caextract.html) and are used for HTTPS connections.

## How to update

1. Download the latest version of the certificate bundle along with its sha256 from [CURL's website](https://curl.se/docs/caextract.html).
2. Replace the `cacert.pem` file in this directory with the new one.
3. Replace the `cacert.sha256` file in this directory with the new one.
4. Update the `version.txt` file with the new version you downloaded.
5. Run `gen_crt_bundle.py` to generate the new `x509_crt_bundle` file.
6. Move the new `x509_crt_bundle` file to the `data/cert` directory.
7. Commit the changes.
