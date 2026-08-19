#!/usr/bin/env python3
#
# OpenShock CA trust store manager. Two subcommands:
#
#   generate  (default) — download curl's CA bundle, SHA-256-verify it, and write two files:
#                         cacert-curl.pem (the verbatim verified base) and cacert-merged.pem
#                         (base + pinned_certs/*.pem + local custom_certs/*.pem). Does NOT
#                         emit a packed x509_crt_bundle; the firmware build converts and
#                         packs cacert-merged.pem into flash via ESP-IDF's certificate-bundle step.
#   audit               — audit every cert already in cacert-merged.pem (base + pinned + custom):
#                         flag expiry, and for pinned roots probe the live endpoints to
#                         decide which are still needed. Reports pins no endpoint chains
#                         through as removable, and raises an alert on a still-needed cert
#                         that is expired/expiring. Used by CI.
#
# Based on Espressif's original certificate bundle generation utility, extended to
# add SHA-256 verification, deprecation-warning surfacing, atomic writes, pinned/custom
# roots, and the audit subcommand.
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
import datetime
import hashlib
import os
import re
import subprocess
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
        'cert_bundle.py: cryptography package not installed ' '(pip install cryptography)',
        file=sys.stderr,
    )
    raise

URL_PEM = 'https://curl.se/ca/cacert.pem'
URL_SHA256 = 'https://curl.se/ca/cacert.pem.sha256'
MANUAL_HASH_PAGE = 'https://curl.se/docs/caextract.html'

# The verbatim curl/Mozilla CA extract, saved exactly as downloaded so `sha256sum` of
# this file matches curl's published checksum (manual verification, see README).
BASE_PEM_FILE = 'cacert-curl.pem'

# The merged trust store packed into firmware (sdkconfig.defaults:
# CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH): the verified curl base followed by every
# pinned (and any local custom) cert.
MERGED_PEM_FILE = 'cacert-merged.pem'

# Banner placed at the top of the merged bundle, in place of curl's own header, so it is
# never mistaken for the verbatim curl extract (its sha256 intentionally differs).
MERGED_BANNER = (
    b'##\n'
    b'## OpenShock firmware TLS trust store -- MERGED / GENERATED, do not edit by hand.\n'
    b'##\n'
    b'## Produced by certificates/cert_bundle.py. This is what the firmware packs:\n'
    b'##   the verbatim curl/Mozilla CA extract (see cacert-curl.pem) + certificates/pinned_certs/.\n'
    b'## Its sha256 intentionally differs from curl\'s published hash. To change it, edit\n'
    b'## pinned_certs/ and re-run `cert_bundle.py generate` -- never hand-edit this file.\n'
    b'##\n'
)

# Pinned certs (tracked, shipped to every device, expiry-monitored). Roots the
# Mozilla/curl base has retired but our infrastructure still chains through -- e.g.
# GlobalSign Root CA R1, which cross-signs GTS Root R4 in the api.openshock.app chain.
PINNED_CERTS_DIR = 'pinned_certs'
PINNED_CERTS_GLOB = '*.pem'

# Custom certs (self-hosted/internal PKI). Local-only, git-ignored.
CUSTOM_CERTS_DIR = 'custom_certs'  # optional; if missing -> ignored
CUSTOM_CERTS_GLOB = '*.pem'  # required extension

# A pinned cert this close to (or past) expiry is a problem: generation hard-fails on an
# already-expired pin; the `audit` subcommand / the CI workflow raise an alert once a
# still-needed pin is within this window. Override with PINNED_CERT_EXPIRY_WARN_DAYS.
EXPIRY_WARN_DAYS_DEFAULT = 90

PEM_BLOCK_RE = re.compile(
    br'-----BEGIN CERTIFICATE-----\s*[\s\S]+?\s*-----END CERTIFICATE-----\s*',
    re.MULTILINE,
)


class InputError(RuntimeError):
    pass


def critical(msg: str) -> None:
    sys.stderr.write('cert_bundle.py: ' + msg + '\n')


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

    # Allow leading label / comment lines before the certificate (curl's bundle prefixes
    # each cert with a human-readable name), but nothing may follow it. Private-key material
    # is rejected globally above, and the single-block checks below reject extra certs.
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


def load_dir_cert_blocks(dirname: str, glob: str) -> tuple[list[bytes], list[Path]]:
    """
    Load one-cert-per-file PEM blocks from a directory.

    Returns: (blocks, paths)

    Behavior:
      - If the directory does not exist -> return ([], [])
      - Only files matching `glob` are considered
      - If the directory exists but contains no matches -> return ([], [])
      - Each file must contain exactly one certificate
    """
    d = Path(dirname)
    if not d.exists():
        return ([], [])

    if not d.is_dir():
        raise InputError(f'{dirname} exists but is not a directory')

    paths = sorted(d.glob(glob))
    if not paths:
        return ([], [])

    critical(f'Loading certificates from: {dirname}/ ({len(paths)} file(s))')

    blocks: list[bytes] = []
    for p in paths:
        if p.suffix.lower() != '.pem':
            # Should not happen due to glob, but keep strict.
            raise InputError(f'Cert file must have .pem extension: {p}')
        blocks.append(_load_single_pem_cert_block_from_file(p))

    return (blocks, paths)


def load_pinned_cert_blocks() -> tuple[list[bytes], list[Path]]:
    """Load tracked, shipped pinned CA certs from pinned_certs/*.pem."""
    return load_dir_cert_blocks(PINNED_CERTS_DIR, PINNED_CERTS_GLOB)


def load_custom_cert_blocks() -> tuple[list[bytes], list[Path]]:
    """Load local-only custom CA certs from custom_certs/*.pem."""
    return load_dir_cert_blocks(CUSTOM_CERTS_DIR, CUSTOM_CERTS_GLOB)


def _cert_not_after_utc(cert: x509.Certificate) -> datetime.datetime:
    # cryptography >= 42 exposes tz-aware not_valid_after_utc; older exposes naive UTC.
    try:
        return cert.not_valid_after_utc
    except AttributeError:
        return cert.not_valid_after.replace(tzinfo=datetime.timezone.utc)


def check_certs_expiry(
    certs: list[x509.Certificate],
    origins: list[str],
    warn_days: int,
) -> tuple[list[str], list[str]]:
    """
    Classify certs by expiry against `warn_days`.

    Returns (expired, expiring) as human-readable descriptions:
      - expired:  notAfter is in the past
      - expiring: notAfter is within `warn_days` from now (but not yet expired)
    Emits a diagnostic line for each problem cert.
    """
    now = datetime.datetime.now(datetime.timezone.utc)
    expired: list[str] = []
    expiring: list[str] = []

    for idx, cert in enumerate(certs):
        origin = origins[idx] if idx < len(origins) else ''
        not_after = _cert_not_after_utc(cert)
        days_left = (not_after - now).days
        desc = f'{cert.subject.rfc4514_string()} (origin: {origin}; notAfter: {not_after:%Y-%m-%d}; {days_left}d left)'

        if days_left < 0:
            critical(f'EXPIRED certificate: {desc}')
            expired.append(desc)
        elif days_left <= warn_days:
            critical(f'WARNING: certificate expires within {warn_days}d: {desc}')
            expiring.append(desc)

    return (expired, expiring)


def _expiry_warn_days() -> int:
    raw = os.environ.get('PINNED_CERT_EXPIRY_WARN_DAYS')
    if not raw:
        return EXPIRY_WARN_DAYS_DEFAULT
    try:
        val = int(raw)
    except ValueError:
        raise InputError(f'PINNED_CERT_EXPIRY_WARN_DAYS must be an integer, got {raw!r}')
    if val < 0:
        raise InputError('PINNED_CERT_EXPIRY_WARN_DAYS must be >= 0')
    return val


def _reject_non_ca(certs: list[x509.Certificate], origins: list[str], *, kind: str) -> None:
    non_ca = [(c, o) for (c, o) in zip(certs, origins) if not _is_ca_certificate(c)]
    if not non_ca:
        return
    critical(f'FATAL: One or more {kind} certificates are not CA certificates (BasicConstraints CA=TRUE missing)')
    for c, origin in non_ca:
        critical(f'  Origin : {origin}')
        critical(f'  Subject: {c.subject.rfc4514_string()}')
        critical(f'  Issuer : {c.issuer.rfc4514_string()}')
    raise InputError(f'Refusing to include non-CA {kind} certificates')


# --------------------------------------------------------------------------------------
# audit — expiry of every shipped cert + live-endpoint relevance for pinned roots
# --------------------------------------------------------------------------------------

# Endpoints whose live chains decide whether a pinned root still earns its place. esp_crt_bundle
# does not do path building: it trusts the chain as the server sends it and looks up the issuer
# of the topmost presented cert among the trusted roots. So the anchor an endpoint "requires" is
# precisely the issuer of the last cert in its presented chain.
MONITORED_DOMAINS = ['api.openshock.app', 'api.openshock.dev']
PROBE_TIMEOUT_S = 20


def _openssl_name(pem_or_der: bytes, field: str) -> str:
    """Canonical (rfc2253) -subject/-issuer string for a single cert, via openssl."""
    proc = subprocess.run(
        ['openssl', 'x509', '-noout', f'-{field}', '-nameopt', 'rfc2253'],
        input=pem_or_der,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise InputError(f'openssl x509 -{field} failed: {proc.stderr.decode(errors="replace")}')
    out = proc.stdout.decode(errors='replace').strip()
    return out.split('=', 1)[1].strip() if '=' in out else out


def _rfc2253_subject(cert: x509.Certificate) -> str:
    return _openssl_name(cert.public_bytes(serialization.Encoding.PEM), 'subject')


def required_anchor_subject(domain: str) -> str | None:
    """
    rfc2253 subject of the anchor `domain` requires (issuer of the topmost cert it
    presents), or None if the endpoint is unreachable.
    """
    try:
        proc = subprocess.run(
            ['openssl', 's_client', '-connect', f'{domain}:443', '-servername', domain, '-showcerts'],
            input=b'',
            capture_output=True,
            timeout=PROBE_TIMEOUT_S,
        )
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        critical(f'  probe error for {domain}: {e}')
        return None

    chain = re.findall(rb'-----BEGIN CERTIFICATE-----.+?-----END CERTIFICATE-----', proc.stdout, re.S)
    if not chain:
        critical(f'  {domain}: no certificates presented (unreachable / handshake failed)')
        return None
    return _openssl_name(chain[-1], 'issuer')


def _emit_gh_outputs(*, removable: list[str], alert: bool, probe_ok: bool) -> None:
    gh_out = os.environ.get('GITHUB_OUTPUT')
    if not gh_out:
        return
    with open(gh_out, 'a', encoding='utf-8') as f:
        f.write(f'removable={" ".join(removable)}\n')
        f.write(f'alert={"true" if alert else "false"}\n')
        f.write(f'probe_ok={"true" if probe_ok else "false"}\n')


def audit() -> int:
    """
    Audit every cert currently in the shipped bundle (cacert-merged.pem = base + pinned + custom):
      - expiry of all of them;
      - for pinned roots, probe the live endpoints to decide which are still needed.

    Emits GitHub outputs (removable / alert / probe_ok). Returns exit code 1 on a genuine
    rotate-now alert (a still-needed cert expired/expiring); removal of unneeded pins is
    left to the workflow's PR step keyed on the `removable` output. Fail-safe: if any
    monitored endpoint is unreachable, nothing is proposed for removal.
    """
    warn_days = _expiry_warn_days()

    # The bundle as actually shipped.
    try:
        bundle_bytes = Path(MERGED_PEM_FILE).read_bytes()
    except OSError as e:
        raise InputError(f'Cannot read {MERGED_PEM_FILE}: {e}') from e
    bundle_blocks = extract_pem_cert_blocks(bundle_bytes, source=MERGED_PEM_FILE)
    bundle_certs = load_all_certs(bundle_blocks, label='bundle')

    # Pinned roots, keyed by canonical subject, so we can tell them apart from the curl
    # base within the merged bundle and know which file to retire.
    pinned_blocks, pinned_paths = load_pinned_cert_blocks()
    pinned_by_subject: dict[str, str] = {}
    for blk, path in zip(pinned_blocks, pinned_paths):
        pinned_by_subject[_openssl_name(blk, 'subject')] = path.as_posix()

    critical(f'Auditing {len(bundle_certs)} certificate(s) in {MERGED_PEM_FILE} '
             f'({len(pinned_by_subject)} pinned); warn window {warn_days}d')

    # Which anchors do the live endpoints actually require?
    required: set[str] = set()
    probe_ok = True
    for domain in MONITORED_DOMAINS:
        subj = required_anchor_subject(domain)
        if subj is None:
            probe_ok = False
            critical(f'  {domain}: UNREACHABLE (will not retire any pin this run)')
            continue
        required.add(subj)
        critical(f'  {domain}: requires anchor -> {subj}')

    now = datetime.datetime.now(datetime.timezone.utc)
    removable: list[str] = []
    alert = False

    for cert in bundle_certs:
        subject = _rfc2253_subject(cert)
        days_left = (_cert_not_after_utc(cert) - now).days
        pinned_path = pinned_by_subject.get(subject)

        if pinned_path is not None:
            needed = subject in required
            if not needed:
                if probe_ok:
                    critical(f'REMOVABLE pin (no endpoint chains through it): {pinned_path} [{subject}]')
                    removable.append(pinned_path)
                else:
                    critical(f'unused pin, but a probe failed -> keeping (fail-safe): {pinned_path}')
                continue
            # Still needed -> expiry is an operational risk.
            if days_left < 0:
                critical(f'ALERT: pinned root EXPIRED and still required: {pinned_path} [{subject}]')
                alert = True
            elif days_left <= warn_days:
                critical(f'ALERT: pinned root expires in {days_left}d and still required: {pinned_path} [{subject}]')
                alert = True
        else:
            # Curl/base (or committed custom) root. We can't retire these individually, but a
            # shipped root that is already expired is still worth failing on.
            if days_left < 0:
                critical(f'ALERT: shipped root EXPIRED: {subject}')
                alert = True
            elif days_left <= warn_days:
                critical(f'note: base root expires in {days_left}d (curl refresh will handle it): {subject}')

    critical('')
    critical(f'Summary: {len(removable)} removable pin(s), alert={alert}, probe_ok={probe_ok}')
    _emit_gh_outputs(removable=removable, alert=alert, probe_ok=probe_ok)
    return 1 if alert else 0


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


def validate_unique_subjects(certs: list[x509.Certificate]) -> None:
    """
    esp_crt_bundle (the ESP-IDF build step that packs cacert-merged.pem into flash) looks
    certificates up by their DER-encoded subject name, so two certs sharing a subject
    would make that lookup ambiguous. Reject duplicates here.
    """
    seen: dict[bytes, x509.Certificate] = {}
    for crt in certs:
        subject_der = crt.subject.public_bytes(serialization.Encoding.DER)
        if subject_der in seen:
            raise InputError(
                'Duplicate certificate subject name detected; this can make '
                f'esp_crt_bundle subject-name lookup ambiguous: {crt.subject.rfc4514_string()}'
            )
        seen[subject_der] = crt


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


def _append_pem_blocks(base: bytes, blocks: list[bytes]) -> bytes:
    out = bytearray(base)
    for blk in blocks:
        if out and not out.endswith(b'\n'):
            out += b'\n'
        out += b'\n' + blk
        if not out.endswith(b'\n'):
            out += b'\n'
    return bytes(out)


def generate() -> int:
    warn_days = _expiry_warn_days()

    critical(f'Downloading CA bundle from: {URL_PEM}')
    pem = download_bytes(URL_PEM)

    got_hash = hashlib.sha256(pem).hexdigest().lower()
    critical(f'Downloaded {len(pem)} bytes')
    critical(f'SHA-256(curl download) = {got_hash}')

    critical(f'Downloading published hash from: {URL_SHA256}')
    sha_file = download_bytes(URL_SHA256)
    expected_hash = parse_sha256_file(sha_file, expect_filename='cacert.pem')
    critical(f'Published SHA-256      = {expected_hash}')

    critical('')
    critical('MANUAL VERIFICATION REQUIRED')
    critical('  Compare the "SHA-256(curl download)" hash above (the upstream bytes, computed')
    critical('  BEFORE pinned/custom certs are appended) against:')
    critical(f'    {MANUAL_HASH_PAGE}')
    critical('')

    if got_hash != expected_hash:
        raise InputError('SHA-256 mismatch against cacert.pem.sha256 — refusing to proceed')

    critical('Automatic SHA-256 validation passed')

    # Curl/Mozilla base.
    blocks = extract_pem_cert_blocks(pem, source='downloaded curl CA bundle')
    curl_certs = load_all_certs(blocks, label='CA', origins=['curl bundle'] * len(blocks))
    critical(f'Found {len(blocks)} certificate blocks in curl bundle')

    # Pinned roots (tracked, shipped, appended into cacert-merged.pem, expiry-monitored).
    pinned_blocks, pinned_paths = load_pinned_cert_blocks()
    pinned_certs: list[x509.Certificate] = []
    if pinned_blocks:
        pinned_origins = [f'{PINNED_CERTS_DIR}/{p.name}' for p in pinned_paths]
        pinned_certs = load_all_certs(pinned_blocks, label='pinned', origins=pinned_origins)
        _reject_non_ca(pinned_certs, pinned_origins, kind='pinned')

        # Never ship an already-dead anchor.
        expired, expiring = check_certs_expiry(pinned_certs, pinned_origins, warn_days)
        if expired:
            raise InputError(f'Refusing to ship {len(expired)} expired pinned certificate(s)')
        if expiring:
            critical(f'NOTE: {len(expiring)} pinned cert(s) expire within {warn_days}d — rotate soon')
        critical(f'Found {len(pinned_blocks)} pinned certificate(s)')
    else:
        critical(f'No pinned certificates found ({PINNED_CERTS_DIR}/*.pem)')

    # Custom roots (local-only, git-ignored self-hosted PKI).
    custom_blocks, custom_paths = load_custom_cert_blocks()
    custom_certs: list[x509.Certificate] = []
    if custom_blocks:
        custom_origins = [f'custom cert file: {p}' for p in custom_paths]
        custom_certs = load_all_certs(custom_blocks, label='custom', origins=custom_origins)
        _reject_non_ca(custom_certs, custom_origins, kind='custom')
        critical(f'Found {len(custom_blocks)} custom certificate(s)')
    else:
        critical('No custom certificates found (custom_certs/*.pem)')

    # Deduplicate: drop any pinned/custom cert whose DER is already present (in the base
    # or an earlier pin/custom) so the merged output never contains the same cert twice.
    seen_der = {c.public_bytes(serialization.Encoding.DER) for c in curl_certs}
    extra_blocks: list[bytes] = []
    extra_certs: list[x509.Certificate] = []
    for blk, cert, origin in zip(
        list(pinned_blocks) + list(custom_blocks),
        pinned_certs + custom_certs,
        [f'{PINNED_CERTS_DIR}/{p.name}' for p in pinned_paths] + [f'custom_certs/{p.name}' for p in custom_paths],
    ):
        der = cert.public_bytes(serialization.Encoding.DER)
        if der in seen_der:
            critical(f'Skipping duplicate certificate (already in bundle): {cert.subject.rfc4514_string()} [{origin}]')
            continue
        seen_der.add(der)
        extra_blocks.append(blk)
        extra_certs.append(cert)

    # Validate the deduplicated set: unique subject names so esp_crt_bundle's subject-name
    # lookup stays unambiguous (this also catches two *different* certs sharing a subject).
    certs = curl_certs + extra_certs
    critical(f'Bundle: {len(curl_certs)} base + {len(extra_certs)} pinned/custom = {len(certs)} certs')
    validate_unique_subjects(certs)

    # Save the verbatim curl base (so `sha256sum {BASE_PEM_FILE}` matches curl's published
    # hash) and the merged store the firmware actually packs. The firmware build converts
    # and packs cacert-merged.pem via ESP-IDF's certificate-bundle step; this script does not emit
    # the packed x509 bundle itself.
    critical(f'Saving {BASE_PEM_FILE} (verbatim curl base, {len(blocks)} certs, atomic)')
    atomic_write(BASE_PEM_FILE, pem)

    # Merged file gets our own banner in place of curl's leading header, then all certs.
    base_no_header = pem[pem.find(b'-----BEGIN CERTIFICATE-----') :]
    combined = MERGED_BANNER + _append_pem_blocks(base_no_header, extra_blocks)
    critical(f'Saving {MERGED_PEM_FILE} ({len(certs)} certs, atomic)')
    atomic_write(MERGED_PEM_FILE, combined)

    critical('Done')
    return 0


USAGE = 'usage: cert_bundle.py [generate|audit]'


def main() -> int:
    args = sys.argv[1:]
    if not args or args == ['generate']:
        return generate()
    if args == ['audit']:
        return audit()
    if args in (['-h'], ['--help']):
        critical(USAGE)
        critical('  generate  (default)  download + verify; write cacert-curl.pem (base) + cacert-merged.pem (base+pinned+custom)')
        critical('  audit                audit expiry of every shipped cert + probe endpoints for pinned')
        critical('                       relevance; exits 1 on a still-needed expired/expiring cert')
        return 0
    raise InputError(USAGE)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except InputError as e:
        critical(str(e))
        raise SystemExit(2)
