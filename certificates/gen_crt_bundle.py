#!/usr/bin/env python3
#
# ESP32 x509 certificate bundle download and conversion script with SHA-256 Verification
#
# This file is based on Espressif's original certificate bundle
# generation utility, but has been modified to:
#  - Automatically download and convert curl's CA certificate bundle
#  - Perform automatic SHA-256 integrity verification of downloads
#  - Emit warnings for certificates that trigger cryptography deprecation notices
#  - Write the output bundle atomically to avoid partial updates
#  - Optionally include user-provided custom CA certificates from custom_certs/*.pem
#
# Copyright 2018-2019 Espressif Systems (Shanghai) PTE LTD
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


from __future__ import annotations

import base64
import hashlib
import os
import re
import struct
import sys
import tempfile
import urllib.request
import ssl
import warnings
from pathlib import Path

try:
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization
    from cryptography.utils import CryptographyDeprecationWarning
except ImportError:
    print(
        'gen_crt_bundle.py: cryptography package not installed ' '(pip install cryptography)',
        file=sys.stderr,
    )
    raise

URL_PEM = 'https://curl.se/ca/cacert.pem'
URL_SHA256 = 'https://curl.se/ca/cacert.pem.sha256'
MANUAL_HASH_PAGE = 'https://curl.se/docs/caextract.html'
OUTPUT_FILE = 'x509_crt_bundle'
LOCAL_PEM_FILE = 'cacert.pem'

# Custom certs (self-hosted/internal PKI)
CUSTOM_CERTS_DIR = 'custom_certs'  # optional; if missing -> ignored
CUSTOM_CERTS_GLOB = '*.pem'  # required extension

PEM_BLOCK_RE = re.compile(
    br'-----BEGIN CERTIFICATE-----\s*[\s\S]+?\s*-----END CERTIFICATE-----\s*',
    re.MULTILINE,
)


class InputError(RuntimeError):
    pass


def critical(msg: str) -> None:
    sys.stderr.write('gen_crt_bundle.py: ' + msg + '\n')


def download_bytes(url: str, timeout: int = 30) -> bytes:
    ctx = ssl.create_default_context()
    req = urllib.request.Request(
        url,
        headers={
            'User-Agent': 'esp32-gen-crt-bundle/strict',
            'Accept': 'text/plain,*/*',
        },
        method='GET',
    )
    with urllib.request.urlopen(req, context=ctx, timeout=timeout) as resp:
        # urllib responses vary; handle both .status and .getcode()
        status = getattr(resp, 'status', None)
        if status is None:
            try:
                status = resp.getcode()
            except Exception:
                status = 200
        if status != 200:
            raise InputError(f'Download failed: HTTP {status} for {url}')
        data = resp.read()
        if not data:
            raise InputError(f'Download failed: empty response from {url}')
        return data


def parse_sha256_file(content: bytes, *, expect_filename: str | None = None) -> str:
    """
    Parse a .sha256 file.
    Common formats include:
      - '<hash>  cacert.pem'
      - '<hash>'
    We accept the first line and extract a leading 64-hex hash.
    If expect_filename is provided and a filename appears on the line,
    we sanity-check that it matches.
    """
    text = content.decode('utf-8', errors='replace').strip()
    if not text:
        raise InputError('Downloaded sha256 file is empty')

    first = text.splitlines()[0].strip()
    m = re.match(r'^([0-9a-fA-F]{64})(?:\s+[* ]?(.+))?$', first)
    if not m:
        raise InputError(f'Could not parse SHA-256 from sha256 file: {first!r}')

    h = m.group(1).lower()
    fn = (m.group(2) or '').strip()

    if expect_filename and fn:
        # Some sha256sum formats include full paths; compare basename.
        if os.path.basename(fn) != expect_filename:
            raise InputError(f'SHA-256 file refers to unexpected filename {fn!r} (expected {expect_filename!r})')

    return h


def extract_pem_cert_blocks(pem_bytes: bytes, *, source: str) -> list[bytes]:
    blocks = PEM_BLOCK_RE.findall(pem_bytes)
    if not blocks:
        raise InputError(f'No PEM certificate blocks found in {source}')
    return blocks


def _load_single_pem_cert_block_from_bytes_strict(data: bytes, *, source: str) -> bytes:
    # Hard fail if file contains any private key material.
    if b'PRIVATE KEY' in data:
        raise InputError(f'Custom cert file contains private key material (PRIVATE KEY): {source}')

    begin = b'-----BEGIN CERTIFICATE-----'
    end = b'-----END CERTIFICATE-----'

    begin_idx = data.find(begin)
    end_idx = data.find(end)
    if begin_idx == -1 or end_idx == -1:
        raise InputError(f'Custom cert file must contain exactly 1 certificate: {source}')

    end_idx += len(end)

    # Disallow any non-whitespace bytes before/after the public certificate
    if data[:begin_idx].strip(b' \t\r\n'):
        raise InputError(f'Non-whitespace data before certificate in {source}')
    if data[end_idx:].strip(b' \t\r\n'):
        raise InputError(f'Non-whitespace data after certificate in {source}')

    # Exactly one cert block (no extra PEM blocks)
    if data.count(begin) != 1 or data.count(end) != 1:
        raise InputError(f'Custom cert file must contain exactly 1 certificate: {source}')
    tmp = data.replace(begin, b'')
    if b'-----BEGIN ' in tmp:
        raise InputError(f'Custom cert file contains additional PEM blocks besides the certificate: {source}')

    # Validate base64 payload decodes cleanly
    pem = data[begin_idx:end_idx]
    lines = pem.splitlines()
    if not lines or lines[0].strip() != begin or lines[-1].strip() != end:
        raise InputError(f'Malformed CERTIFICATE PEM boundaries in {source}')

    b64_lines = [ln.strip() for ln in lines[1:-1] if ln.strip()]
    if not b64_lines:
        raise InputError(f'Empty certificate payload in {source}')

    b64_blob = b''.join(b64_lines)
    try:
        der = base64.b64decode(b64_blob, validate=True)
    except Exception as e:
        raise InputError(f'Invalid base64 payload in certificate: {source}') from e
    if not der or len(der) < 64:
        raise InputError(f'Certificate DER payload too small / invalid: {source}')

    # Canonicalize output PEM (stable formatting)
    b64_canon = base64.b64encode(der)
    out_lines = [b64_canon[i : i + 64] for i in range(0, len(b64_canon), 64)]
    return b'-----BEGIN CERTIFICATE-----\n' + b'\n'.join(out_lines) + b'\n-----END CERTIFICATE-----\n'


def _load_single_pem_cert_block_from_file(path: Path) -> bytes:
    try:
        data = path.read_bytes()
    except Exception as e:
        raise InputError(f'Failed to read custom cert file: {path}') from e

    return _load_single_pem_cert_block_from_bytes_strict(data, source=f'custom cert file: {path}')


def load_custom_cert_blocks() -> tuple[list[bytes], list[Path]]:
    """
    Load user-provided custom CA certificate PEM blocks from custom_certs/*.pem.

    Returns: (blocks, paths)

    Behavior:
      - If custom_certs/ does not exist -> return ([], [])
      - Only *.pem files are considered
      - If the directory exists but contains no *.pem -> return ([], [])
      - Non-.pem files are ignored (we do not scan them)
      - Each .pem must contain exactly one certificate
    """
    d = Path(CUSTOM_CERTS_DIR)
    if not d.exists():
        return ([], [])

    if not d.is_dir():
        raise InputError(f'{CUSTOM_CERTS_DIR} exists but is not a directory')

    paths = sorted(d.glob(CUSTOM_CERTS_GLOB))
    if not paths:
        return ([], [])

    critical(f'Loading custom certificates from: {CUSTOM_CERTS_DIR}/ ({len(paths)} file(s))')

    blocks: list[bytes] = []
    for p in paths:
        if p.suffix.lower() != '.pem':
            # Should not happen due to glob, but keep strict.
            raise InputError(f'Custom cert file must have .pem extension: {p}')
        blocks.append(_load_single_pem_cert_block_from_file(p))

    return (blocks, paths)


def _is_ca_certificate(cert: x509.Certificate) -> bool:
    """
    Best-effort CA check:
      - Requires BasicConstraints CA=TRUE
    (We keep this strict to avoid adding leaf/server certs by mistake.)
    """
    try:
        bc = cert.extensions.get_extension_for_class(x509.BasicConstraints).value
    except Exception:
        return False
    return bool(getattr(bc, 'ca', False))


def load_all_certs(
    blocks: list[bytes],
    *,
    label: str = 'CA',
    origins: list[str] | None = None,
) -> list[x509.Certificate]:
    certs: list[x509.Certificate] = []

    for idx, b in enumerate(blocks, start=1):
        origin = ''
        if origins and 0 <= (idx - 1) < len(origins):
            origin = origins[idx - 1]

        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter('always', CryptographyDeprecationWarning)

            try:
                cert = x509.load_pem_x509_certificate(b)
            except Exception as e:
                critical(f'FATAL: Invalid certificate at index {idx} ({label})')
                if origin:
                    critical(f'  Origin : {origin}')
                critical(f'  Reason: {e}')
                critical('  Offending PEM:')
                critical(b.decode('ascii', errors='replace').rstrip())
                raise InputError('Aborting due to invalid CA certificate') from e

            for warn in w:
                if issubclass(warn.category, CryptographyDeprecationWarning):
                    subject = cert.subject.rfc4514_string()
                    issuer = cert.issuer.rfc4514_string()

                    critical('WARNING: CA certificate triggers CryptographyDeprecationWarning')
                    critical('         This may become fatal in future cryptography releases.')
                    critical(f'  Index   : {idx} ({label})')
                    if origin:
                        critical(f'  Origin  : {origin}')
                    critical(f'  Subject : {subject}')
                    critical(f'  Issuer  : {issuer}')
                    critical(f'  Reason  : {warn.message}')
                    critical('  Offending PEM:')
                    critical(b.decode('ascii', errors='replace').rstrip())

            certs.append(cert)

    return certs


def create_bundle(certs: list[x509.Certificate]) -> bytes:
    def subject_der(c: x509.Certificate) -> bytes:
        return c.subject.public_bytes(serialization.Encoding.DER)

    certs_sorted = sorted(certs, key=subject_der)

    for i in range(1, len(certs_sorted)):
        if subject_der(certs_sorted[i - 1]) == subject_der(certs_sorted[i]):
            raise InputError(
                'Duplicate certificate subject name detected in bundle; ' 'this can make subject-name lookup ambiguous'
            )

    bundle = struct.pack('>H', len(certs_sorted))

    for crt in certs_sorted:
        pub_key_der = crt.public_key().public_bytes(
            serialization.Encoding.DER,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
        sub_name_der = subject_der(crt)

        if len(sub_name_der) > 0xFFFF or len(pub_key_der) > 0xFFFF:
            raise InputError('Subject name or public key too large for bundle format')

        bundle += struct.pack('>HH', len(sub_name_der), len(pub_key_der))
        bundle += sub_name_der
        bundle += pub_key_der

    return bundle


def atomic_write(path: str, data: bytes) -> None:
    out_dir = os.path.dirname(os.path.abspath(path)) or '.'
    os.makedirs(out_dir, exist_ok=True)

    fd = None
    tmp_path = None
    try:
        fd, tmp_path = tempfile.mkstemp(prefix='.tmp_crt_bundle_', dir=out_dir)
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        fd = None

        os.replace(tmp_path, path)
        tmp_path = None
    finally:
        try:
            if tmp_path and os.path.exists(tmp_path):
                os.remove(tmp_path)
        except Exception:
            pass


def main() -> int:
    if len(sys.argv) != 1:
        raise InputError('This script takes no arguments')

    critical(f'Downloading CA bundle from: {URL_PEM}')
    pem = download_bytes(URL_PEM)

    got_hash = hashlib.sha256(pem).hexdigest().lower()
    critical(f'Downloaded {len(pem)} bytes')
    critical(f'SHA-256(cacert.pem) = {got_hash}')

    critical(f'Downloading published hash from: {URL_SHA256}')
    sha_file = download_bytes(URL_SHA256)
    expected_hash = parse_sha256_file(sha_file, expect_filename=os.path.basename(LOCAL_PEM_FILE))
    critical(f'Published SHA-256     = {expected_hash}')

    critical('')
    critical('MANUAL VERIFICATION REQUIRED')
    critical('  Compare the above SHA-256 hash against:')
    critical(f'    {MANUAL_HASH_PAGE}')
    critical('')

    if got_hash != expected_hash:
        raise InputError('SHA-256 mismatch against cacert.pem.sha256 — refusing to proceed')

    critical('Automatic SHA-256 validation passed')

    # Only write cacert.pem after verification passes
    critical(f'Saving verified PEM to {LOCAL_PEM_FILE} (atomic)')
    atomic_write(LOCAL_PEM_FILE, pem)

    blocks = extract_pem_cert_blocks(pem, source='downloaded curl CA bundle (cacert.pem)')
    critical(f'Found {len(blocks)} certificate blocks in curl bundle')

    (custom_blocks, custom_paths) = load_custom_cert_blocks()
    custom_certs: list[x509.Certificate] = []
    if custom_blocks:
        critical(f'Found {len(custom_blocks)} custom certificate(s)')
        custom_origins = [f'custom cert file: {p}' for p in custom_paths]
        custom_certs = load_all_certs(custom_blocks, label='custom', origins=custom_origins)

        # Enforce that custom certs are CA certs (best-effort) and prevent accidental leaf addition.
        non_ca = [(c, o) for (c, o) in zip(custom_certs, custom_origins) if not _is_ca_certificate(c)]
        if non_ca:
            critical(
                'FATAL: One or more custom certificates are not CA certificates (BasicConstraints CA=TRUE missing)'
            )
            for c, origin in non_ca:
                critical(f'  Origin : {origin}')
                critical(f'  Subject: {c.subject.rfc4514_string()}')
                critical(f'  Issuer : {c.issuer.rfc4514_string()}')
            raise InputError('Refusing to include non-CA custom certificates')
    else:
        critical('No custom certificates found (custom_certs/*.pem)')

    curl_origins = ['curl bundle'] * len(blocks)
    curl_certs = load_all_certs(blocks, label='CA', origins=curl_origins)
    certs = curl_certs + custom_certs

    critical(f'Parsed {len(certs)} certificates total')

    bundle = create_bundle(certs)
    critical(f'Writing bundle to {OUTPUT_FILE} (atomic)')
    atomic_write(OUTPUT_FILE, bundle)

    critical('Done')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except InputError as e:
        critical(str(e))
        raise SystemExit(2)
