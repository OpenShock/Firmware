#!/usr/bin/env python3
#
# OpenShock CA trust store manager. Two subcommands:
#
#   generate  (default): download curl's CA bundle, SHA-256-verify it, and write two files:
#                         cacert-curl.pem (the verbatim verified base) and cacert-merged.pem
#                         (base + pinned_certs/*.pem + local custom_certs/*.pem). Does NOT
#                         emit a packed x509_crt_bundle; the firmware build converts and
#                         packs cacert-merged.pem into flash via ESP-IDF's certificate-bundle step.
#   audit:               audit every cert already in cacert-merged.pem (base + pinned + custom):
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

import argparse
import base64
import datetime
import hashlib
import os
import re
import ssl
import subprocess
import sys
import tempfile
import urllib.request
import warnings
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

try:
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization
    from cryptography.utils import CryptographyDeprecationWarning
except ImportError:
    print(
        'cert_bundle.py: cryptography package not installed (pip install cryptography)',
        file=sys.stderr,
    )
    raise

HERE = Path(__file__).resolve().parent

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
    b"## Its sha256 intentionally differs from curl's published hash. To change it, edit\n"
    b'## pinned_certs/ and re-run `cert_bundle.py generate` -- never hand-edit this file.\n'
    b'##\n'
)

# Pinned certs (tracked, shipped to every device, expiry-monitored). Roots the
# Mozilla/curl base has retired but our infrastructure still chains through -- e.g.
# GlobalSign Root CA R1, which cross-signs GTS Root R4 in the api.openshock.app chain.
PINNED_CERTS_DIR = 'pinned_certs'

# Custom certs (self-hosted/internal PKI). Local-only, git-ignored.
CUSTOM_CERTS_DIR = 'custom_certs'  # optional; if missing -> ignored
CERT_GLOB = '*.pem'

# A pinned cert this close to (or past) expiry is a problem: generation hard-fails on an
# already-expired pin; the `audit` subcommand / the CI workflow raise an alert once a
# still-needed pin is within this window. Override with PINNED_CERT_EXPIRY_WARN_DAYS.
EXPIRY_WARN_DAYS_DEFAULT = 90

PEM_BEGIN = b'-----BEGIN CERTIFICATE-----'
PEM_END = b'-----END CERTIFICATE-----'
PEM_BLOCK_RE = re.compile(
    rb'-----BEGIN CERTIFICATE-----\s*[\s\S]+?\s*-----END CERTIFICATE-----\s*',
    re.MULTILINE,
)


class InputError(RuntimeError):
    pass


def log(msg: str) -> None:
    sys.stderr.write('cert_bundle.py: ' + msg + '\n')


def download_bytes(url: str, timeout: int = 30) -> bytes:
    ctx = ssl.create_default_context()
    req = urllib.request.Request(
        url,
        headers={
            'User-Agent': 'openshock-cert-bundle',
            'Accept': 'text/plain,*/*',
        },
        method='GET',
    )
    with urllib.request.urlopen(req, context=ctx, timeout=timeout) as resp:
        if resp.status != 200:
            raise InputError(f'Download failed: HTTP {resp.status} for {url}')
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

    # Some sha256sum formats include full paths; compare basename.
    if expect_filename and fn and os.path.basename(fn) != expect_filename:
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
        raise InputError(f'Cert file contains private key material (PRIVATE KEY): {source}')

    begin, end = PEM_BEGIN, PEM_END

    begin_idx = data.find(begin)
    end_idx = data.find(end)
    if begin_idx == -1 or end_idx == -1:
        raise InputError(f'Cert file must contain exactly 1 certificate: {source}')

    end_idx += len(end)

    # Allow leading label / comment lines before the certificate (curl's bundle prefixes
    # each cert with a human-readable name), but nothing may follow it. Private-key material
    # is rejected globally above, and the single-block checks below reject extra certs.
    if data[end_idx:].strip(b' \t\r\n'):
        raise InputError(f'Non-whitespace data after certificate in {source}')

    # Exactly one cert block (no extra PEM blocks)
    if data.count(begin) != 1 or data.count(end) != 1:
        raise InputError(f'Cert file must contain exactly 1 certificate: {source}')
    tmp = data.replace(begin, b'')
    if b'-----BEGIN ' in tmp:
        raise InputError(f'Cert file contains additional PEM blocks besides the certificate: {source}')

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
    return PEM_BEGIN + b'\n' + b'\n'.join(out_lines) + b'\n' + PEM_END + b'\n'


def _load_single_pem_cert_block_from_file(path: Path) -> bytes:
    try:
        data = (HERE / path).read_bytes()
    except Exception as e:
        raise InputError(f'Failed to read cert file: {path}') from e

    return _load_single_pem_cert_block_from_bytes_strict(data, source=f'cert file: {path}')


@dataclass(frozen=True)
class LoadedCert:
    """One certificate as read from a file: its canonical PEM block, the parsed cert, and where it came from."""

    block: bytes
    cert: x509.Certificate
    origin: str

    @property
    def der(self) -> bytes:
        return self.cert.public_bytes(serialization.Encoding.DER)


def load_dir_cert_blocks(dirname: str, glob: str) -> list[tuple[Path, bytes]]:
    """
    Load one-cert-per-file PEM blocks from a directory, as (path relative to HERE, block) pairs.

    Behavior:
      - If the directory does not exist -> []
      - Only files matching `glob` are considered
      - If the directory exists but contains no matches -> []
      - Each file must contain exactly one certificate
    """
    d = HERE / dirname
    if not d.exists():
        return []

    if not d.is_dir():
        raise InputError(f'{dirname} exists but is not a directory')

    paths = sorted(p.relative_to(HERE) for p in d.glob(glob))
    if paths:
        log(f'Loading certificates from: {dirname}/ ({len(paths)} file(s))')
    return [(p, _load_single_pem_cert_block_from_file(p)) for p in paths]


def check_certs_expiry(
    certs: list[LoadedCert],
    warn_days: int,
    now: datetime.datetime | None = None,
) -> tuple[list[str], list[str]]:
    """
    Classify certs by expiry against `warn_days`.

    Returns (expired, expiring) as human-readable descriptions:
      - expired:  notAfter is in the past
      - expiring: notAfter is within `warn_days` from now (but not yet expired)
    Emits a diagnostic line for each problem cert.
    """
    now = now or datetime.datetime.now(datetime.timezone.utc)
    expired: list[str] = []
    expiring: list[str] = []

    for loaded in certs:
        not_after = loaded.cert.not_valid_after_utc
        days_left = (not_after - now).days
        desc = (
            f'{loaded.cert.subject.rfc4514_string()} '
            f'(origin: {loaded.origin}; notAfter: {not_after:%Y-%m-%d}; {days_left}d left)'
        )

        if days_left < 0:
            log(f'EXPIRED certificate: {desc}')
            expired.append(desc)
        elif days_left <= warn_days:
            log(f'WARNING: certificate expires within {warn_days}d: {desc}')
            expiring.append(desc)

    return (expired, expiring)


def _expiry_warn_days() -> int:
    raw = os.environ.get('PINNED_CERT_EXPIRY_WARN_DAYS')
    if not raw:
        return EXPIRY_WARN_DAYS_DEFAULT
    try:
        val = int(raw)
    except ValueError:
        raise InputError(f'PINNED_CERT_EXPIRY_WARN_DAYS must be an integer, got {raw!r}') from None
    if val < 0:
        raise InputError('PINNED_CERT_EXPIRY_WARN_DAYS must be >= 0')
    return val


def _reject_non_ca(certs: list[LoadedCert], *, kind: str) -> None:
    non_ca = [c for c in certs if not _is_ca_certificate(c.cert)]
    if not non_ca:
        return
    log(f'FATAL: One or more {kind} certificates are not CA certificates (BasicConstraints CA=TRUE missing)')
    for c in non_ca:
        log(f'  Origin : {c.origin}')
        log(f'  Subject: {c.cert.subject.rfc4514_string()}')
        log(f'  Issuer : {c.cert.issuer.rfc4514_string()}')
    raise InputError(f'Refusing to include non-CA {kind} certificates')


# Endpoints whose live chains decide whether a pinned root is still needed: esp_crt_bundle does no
# path building, so the required anchor is the issuer of the last cert in the presented chain.
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
        log(f'  probe error for {domain}: {e}')
        return None

    chain = re.findall(rb'-----BEGIN CERTIFICATE-----.+?-----END CERTIFICATE-----', proc.stdout, re.S)
    if not chain:
        log(f'  {domain}: no certificates presented (unreachable / handshake failed)')
        return None
    return _openssl_name(chain[-1], 'issuer')


def probe_required_anchors(domains: list[str] = MONITORED_DOMAINS) -> tuple[set[str], bool]:
    """The anchor subjects the live endpoints chain through, and whether every probe succeeded."""
    required: set[str] = set()
    probe_ok = True
    for domain in domains:
        subj = required_anchor_subject(domain)
        if subj is None:
            probe_ok = False
            log(f'  {domain}: UNREACHABLE (will not retire any pin this run)')
            continue
        required.add(subj)
        log(f'  {domain}: requires anchor -> {subj}')
    return required, probe_ok


@dataclass(frozen=True)
class AuditResult:
    removable: list[str]  # pinned cert files no endpoint chains through anymore
    alert: bool  # a cert that must stay is expired or about to expire


def classify_bundle(
    bundle: list[tuple[str, x509.Certificate]],
    pinned_by_subject: dict[str, str],
    required: set[str],
    *,
    probe_ok: bool,
    warn_days: int,
    now: datetime.datetime,
) -> AuditResult:
    """
    Decide, for every (rfc2253 subject, cert) in the shipped bundle, whether it can be retired or needs rotating.

    - A base root (not pinned) is never retired here, but an expired one alerts; curl's refresh handles the rest.
    - A pinned root no endpoint requires is removable, but only when every probe succeeded (fail-safe).
    - A pinned root an endpoint requires alerts when it is expired or within `warn_days` of expiry.
    """
    removable: list[str] = []
    alert = False

    for subject, cert in bundle:
        days_left = (cert.not_valid_after_utc - now).days
        pinned_path = pinned_by_subject.get(subject)

        if pinned_path is None:
            if days_left < 0:
                log(f'ALERT: shipped root EXPIRED: {subject}')
                alert = True
            elif days_left <= warn_days:
                log(f'note: base root expires in {days_left}d (curl refresh will handle it): {subject}')
            continue

        if subject not in required:
            if probe_ok:
                log(f'REMOVABLE pin (no endpoint chains through it): {pinned_path} [{subject}]')
                removable.append(pinned_path)
            else:
                log(f'unused pin, but a probe failed -> keeping (fail-safe): {pinned_path}')
            continue

        # Still needed, so expiry is an operational risk.
        if days_left < 0:
            log(f'ALERT: pinned root EXPIRED and still required: {pinned_path} [{subject}]')
            alert = True
        elif days_left <= warn_days:
            log(f'ALERT: pinned root expires in {days_left}d and still required: {pinned_path} [{subject}]')
            alert = True

    return AuditResult(removable, alert)


def _emit_gh_outputs(result: AuditResult) -> None:
    # Standalone on purpose (run as certificates/cert_bundle.py, without scripts/ on the path), so not gha.set_output.
    gh_out = os.environ.get('GITHUB_OUTPUT')
    if not gh_out:
        return
    with open(gh_out, 'a', encoding='utf-8') as f:
        f.write(f'removable={" ".join(result.removable)}\n')
        f.write(f'alert={"true" if result.alert else "false"}\n')


def audit() -> int:
    """
    Audit every cert currently in the shipped bundle (cacert-merged.pem = base + pinned + custom):
      - expiry of all of them;
      - for pinned roots, probe the live endpoints to decide which are still needed.

    Emits GitHub outputs (removable / alert). Returns exit code 1 on a genuine
    rotate-now alert (a still-needed cert expired/expiring); removal of unneeded pins is
    left to the workflow's PR step keyed on the `removable` output. Fail-safe: if any
    monitored endpoint is unreachable, nothing is proposed for removal.
    """
    warn_days = _expiry_warn_days()

    # The bundle as actually shipped.
    try:
        bundle_bytes = (HERE / MERGED_PEM_FILE).read_bytes()
    except OSError as e:
        raise InputError(f'Cannot read {MERGED_PEM_FILE}: {e}') from e
    bundle_certs = load_all_certs(extract_pem_cert_blocks(bundle_bytes, source=MERGED_PEM_FILE), label='bundle')

    # Pinned roots, keyed by canonical subject, so we can tell them apart from the curl
    # base within the merged bundle and know which file to retire.
    pinned_by_subject = {
        _openssl_name(block, 'subject'): path.as_posix()
        for path, block in load_dir_cert_blocks(PINNED_CERTS_DIR, CERT_GLOB)
    }

    log(
        f'Auditing {len(bundle_certs)} certificate(s) in {MERGED_PEM_FILE} '
        f'({len(pinned_by_subject)} pinned); warn window {warn_days}d'
    )

    required, probe_ok = probe_required_anchors()
    result = classify_bundle(
        [(_rfc2253_subject(cert), cert) for cert in bundle_certs],
        pinned_by_subject,
        required,
        probe_ok=probe_ok,
        warn_days=warn_days,
        now=datetime.datetime.now(datetime.timezone.utc),
    )

    log('')
    log(f'Summary: {len(result.removable)} removable pin(s), alert={result.alert}, probe_ok={probe_ok}')
    _emit_gh_outputs(result)
    return 1 if result.alert else 0


def _is_ca_certificate(cert: x509.Certificate) -> bool:
    """
    Best-effort CA check:
      - Requires BasicConstraints CA=TRUE
    (We keep this strict to avoid adding leaf/server certs by mistake.)
    """
    try:
        return cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca
    except x509.ExtensionNotFound:
        return False


def load_all_certs(
    blocks: list[bytes],
    *,
    label: str = 'CA',
    origins: list[str] | None = None,
) -> list[x509.Certificate]:
    certs: list[x509.Certificate] = []

    for idx, b in enumerate(blocks, start=1):
        origin = origins[idx - 1] if origins else ''

        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter('always', CryptographyDeprecationWarning)

            try:
                cert = x509.load_pem_x509_certificate(b)
            except Exception as e:
                log(f'FATAL: Invalid certificate at index {idx} ({label})')
                if origin:
                    log(f'  Origin : {origin}')
                log(f'  Reason: {e}')
                log('  Offending PEM:')
                log(b.decode('ascii', errors='replace').rstrip())
                raise InputError('Aborting due to invalid CA certificate') from e

            for warn in w:
                if issubclass(warn.category, CryptographyDeprecationWarning):
                    subject = cert.subject.rfc4514_string()
                    issuer = cert.issuer.rfc4514_string()

                    log('WARNING: CA certificate triggers CryptographyDeprecationWarning')
                    log('         This may become fatal in future cryptography releases.')
                    log(f'  Index   : {idx} ({label})')
                    if origin:
                        log(f'  Origin  : {origin}')
                    log(f'  Subject : {subject}')
                    log(f'  Issuer  : {issuer}')
                    log(f'  Reason  : {warn.message}')
                    log('  Offending PEM:')
                    log(b.decode('ascii', errors='replace').rstrip())

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


def atomic_write(path: Path, data: bytes) -> None:
    out_dir = path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    fd = None
    tmp_path = None
    try:
        fd, tmp_name = tempfile.mkstemp(prefix='.tmp_crt_bundle_', dir=out_dir)
        tmp_path = Path(tmp_name)
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        fd = None

        tmp_path.replace(path)
        tmp_path = None
    finally:
        if tmp_path:
            tmp_path.unlink(missing_ok=True)


def _append_pem_blocks(base: bytes, blocks: list[bytes]) -> bytes:
    out = bytearray(base)
    for blk in blocks:
        if out and not out.endswith(b'\n'):
            out += b'\n'
        out += b'\n' + blk
        if not out.endswith(b'\n'):
            out += b'\n'
    return bytes(out)


def load_group(dirname: str, kind: str) -> list[LoadedCert]:
    """Load and CA-check one directory of certs; empty when the directory holds none."""
    files = load_dir_cert_blocks(dirname, CERT_GLOB)
    if not files:
        log(f'No {kind} certificates found ({dirname}/{CERT_GLOB})')
        return []
    origins = [path.as_posix() for path, _ in files]
    certs = load_all_certs([block for _, block in files], label=kind, origins=origins)
    loaded = [LoadedCert(block, cert, origin) for (_, block), cert, origin in zip(files, certs, origins, strict=True)]
    _reject_non_ca(loaded, kind=kind)
    log(f'Found {len(loaded)} {kind} certificate(s)')
    return loaded


def dedupe_extra_certs(base: list[x509.Certificate], extras: list[LoadedCert]) -> list[LoadedCert]:
    """The extras whose DER is not already in the base or an earlier extra, so the bundle never holds a cert twice."""
    seen = {c.public_bytes(serialization.Encoding.DER) for c in base}
    kept: list[LoadedCert] = []
    for extra in extras:
        if extra.der in seen:
            log(
                f'Skipping duplicate certificate (already in bundle): {extra.cert.subject.rfc4514_string()} [{extra.origin}]'
            )
            continue
        seen.add(extra.der)
        kept.append(extra)
    return kept


def verified_curl_bundle(download: Callable[[str], bytes]) -> bytes:
    """Download curl's CA extract and refuse it unless it matches the SHA-256 curl publishes beside it."""
    log(f'Downloading CA bundle from: {URL_PEM}')
    pem = download(URL_PEM)

    got_hash = hashlib.sha256(pem).hexdigest().lower()
    log(f'Downloaded {len(pem)} bytes')
    log(f'SHA-256(curl download) = {got_hash}')

    log(f'Downloading published hash from: {URL_SHA256}')
    expected_hash = parse_sha256_file(download(URL_SHA256), expect_filename='cacert.pem')
    log(f'Published SHA-256      = {expected_hash}')

    log('')
    log('MANUAL VERIFICATION REQUIRED')
    log('  Compare the "SHA-256(curl download)" hash above (the upstream bytes, computed')
    log('  BEFORE pinned/custom certs are appended) against:')
    log(f'    {MANUAL_HASH_PAGE}')
    log('')

    if got_hash != expected_hash:
        raise InputError('SHA-256 mismatch against cacert.pem.sha256; refusing to proceed')
    log('Automatic SHA-256 validation passed')
    return pem


def generate(download: Callable[[str], bytes] = download_bytes) -> int:
    warn_days = _expiry_warn_days()
    pem = verified_curl_bundle(download)

    # Curl/Mozilla base.
    blocks = extract_pem_cert_blocks(pem, source='downloaded curl CA bundle')
    curl_certs = load_all_certs(blocks, label='CA', origins=['curl bundle'] * len(blocks))
    log(f'Found {len(blocks)} certificate blocks in curl bundle')

    # Pinned roots (tracked, shipped, expiry-monitored), then custom roots (local-only, git-ignored self-hosted PKI).
    pinned = load_group(PINNED_CERTS_DIR, 'pinned')
    # Never ship an already-dead anchor.
    expired, expiring = check_certs_expiry(pinned, warn_days)
    if expired:
        raise InputError(f'Refusing to ship {len(expired)} expired pinned certificate(s)')
    if expiring:
        log(f'NOTE: {len(expiring)} pinned cert(s) expire within {warn_days}d; rotate soon')
    custom = load_group(CUSTOM_CERTS_DIR, 'custom')

    extras = dedupe_extra_certs(curl_certs, pinned + custom)

    # Validate the deduplicated set: unique subject names so esp_crt_bundle's subject-name
    # lookup stays unambiguous (this also catches two *different* certs sharing a subject).
    certs = curl_certs + [e.cert for e in extras]
    log(f'Bundle: {len(curl_certs)} base + {len(extras)} pinned/custom = {len(certs)} certs')
    validate_unique_subjects(certs)

    # Save the verbatim curl base (so `sha256sum {BASE_PEM_FILE}` matches curl's published
    # hash) and the merged store the firmware actually packs. The firmware build converts
    # and packs cacert-merged.pem via ESP-IDF's certificate-bundle step; this script does not emit
    # the packed x509 bundle itself.
    log(f'Saving {BASE_PEM_FILE} (verbatim curl base, {len(blocks)} certs, atomic)')
    atomic_write(HERE / BASE_PEM_FILE, pem)

    # Merged file gets our own banner in place of curl's leading header, then all certs.
    base_no_header = pem[pem.find(PEM_BEGIN) :]
    combined = MERGED_BANNER + _append_pem_blocks(base_no_header, [e.block for e in extras])
    log(f'Saving {MERGED_PEM_FILE} ({len(certs)} certs, atomic)')
    atomic_write(HERE / MERGED_PEM_FILE, combined)

    log('Done')
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description='OpenShock CA trust store manager.')
    ap.add_argument(
        'command',
        nargs='?',
        choices=['generate', 'audit'],
        default='generate',
        help="generate (default): download + verify curl's bundle, write cacert-curl.pem (base) and "
        'cacert-merged.pem (base+pinned+custom); audit: check expiry of every shipped cert and probe endpoints '
        'for pinned relevance, exit 1 on a still-needed expired/expiring cert',
    )
    return audit() if ap.parse_args(argv).command == 'audit' else generate()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except InputError as e:
        log(str(e))
        raise SystemExit(2) from None
