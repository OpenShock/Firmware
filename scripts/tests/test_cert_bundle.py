"""Tests for the offline parts of certificates/cert_bundle.py: PEM loading and validation, expiry, merging."""

import datetime
import hashlib

import cert_bundle
import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

NOW = datetime.datetime.now(datetime.timezone.utc)


def make_cert(name='Root A', *, ca=True, days_left=365):
    key = ec.generate_private_key(ec.SECP256R1())
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(NOW - datetime.timedelta(days=400))
        .not_valid_after(NOW + datetime.timedelta(days=days_left))
        .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True)
        .sign(key, hashes.SHA256())
    )
    return cert, cert.public_bytes(serialization.Encoding.PEM)


def loaded(name='Root A', *, origin='pinned_certs/a.pem', **kwargs):
    cert, pem = make_cert(name, **kwargs)
    return cert_bundle.LoadedCert(pem, cert, origin)


@pytest.mark.parametrize(
    'content, expected',
    [
        (b'a' * 64 + b'  cacert.pem\n', 'a' * 64),
        (b'A' * 64 + b' *cacert.pem', 'a' * 64),
        (b'b' * 64 + b'\nsecond line ignored', 'b' * 64),
        (b'c' * 64 + b'  /some/dir/cacert.pem', 'c' * 64),
    ],
)
def test_parse_sha256_file(content, expected):
    assert cert_bundle.parse_sha256_file(content, expect_filename='cacert.pem') == expected


@pytest.mark.parametrize('content', [b'', b'  \n', b'not-a-hash cacert.pem', b'a' * 63, b'a' * 64 + b'  other.pem'])
def test_parse_sha256_file_rejects(content):
    with pytest.raises(cert_bundle.InputError):
        cert_bundle.parse_sha256_file(content, expect_filename='cacert.pem')


def test_a_single_cert_block_is_canonicalised_and_labels_are_dropped():
    _, pem = make_cert()
    block = cert_bundle._load_single_pem_cert_block_from_bytes_strict(b'Some Label\n=====\n' + pem, source='x')
    assert block == pem


@pytest.mark.parametrize(
    'mutate',
    [
        lambda pem: pem + pem,
        lambda pem: pem + b'trailing',
        lambda pem: pem + b'-----BEGIN PRIVATE KEY-----\nAAAA\n-----END PRIVATE KEY-----\n',
        lambda pem: b'',
        lambda pem: pem.replace(b'MII', b'!!!', 1),
    ],
)
def test_a_cert_file_must_hold_exactly_one_clean_certificate(mutate):
    _, pem = make_cert()
    with pytest.raises(cert_bundle.InputError):
        cert_bundle._load_single_pem_cert_block_from_bytes_strict(mutate(pem), source='x')


def test_extract_pem_cert_blocks():
    _, a = make_cert('A')
    _, b = make_cert('B')
    assert len(cert_bundle.extract_pem_cert_blocks(b'## banner\n' + a + b, source='x')) == 2
    with pytest.raises(cert_bundle.InputError):
        cert_bundle.extract_pem_cert_blocks(b'nothing', source='x')


def test_expiry_is_classified_against_the_warning_window():
    certs = [loaded(f'C{days}', days_left=days) for days in (-5, 30, 400)]
    expired, expiring = cert_bundle.check_certs_expiry(certs, warn_days=90, now=NOW)
    assert [d.split(' ')[0] for d in expired] == ['CN=C-5']
    assert [d.split(' ')[0] for d in expiring] == ['CN=C30']


@pytest.mark.parametrize('raw, expected', [(None, 90), ('', 90), ('14', 14), ('0', 0)])
def test_expiry_warn_days_from_env(monkeypatch, raw, expected):
    monkeypatch.delenv('PINNED_CERT_EXPIRY_WARN_DAYS', raising=False)
    if raw is not None:
        monkeypatch.setenv('PINNED_CERT_EXPIRY_WARN_DAYS', raw)
    assert cert_bundle._expiry_warn_days() == expected


@pytest.mark.parametrize('raw', ['soon', '-1'])
def test_expiry_warn_days_rejects_bad_values(monkeypatch, raw):
    monkeypatch.setenv('PINNED_CERT_EXPIRY_WARN_DAYS', raw)
    with pytest.raises(cert_bundle.InputError):
        cert_bundle._expiry_warn_days()


def test_non_ca_certificates_are_refused():
    ca = loaded('CA')
    leaf = loaded('Leaf', ca=False)
    cert_bundle._reject_non_ca([ca], kind='pinned')
    with pytest.raises(cert_bundle.InputError, match='non-CA pinned'):
        cert_bundle._reject_non_ca([ca, leaf], kind='pinned')


def test_duplicate_subjects_are_refused():
    a1, _ = make_cert('Same')
    a2, _ = make_cert('Same')
    b, _ = make_cert('Other')
    cert_bundle.validate_unique_subjects([a1, b])
    with pytest.raises(cert_bundle.InputError, match='Duplicate certificate subject'):
        cert_bundle.validate_unique_subjects([a1, b, a2])


def test_load_all_certs_reports_an_unparseable_block():
    _, pem = make_cert()
    assert len(cert_bundle.load_all_certs([pem])) == 1
    with pytest.raises(cert_bundle.InputError, match='invalid CA certificate'):
        cert_bundle.load_all_certs([cert_bundle.PEM_BEGIN + b'\nAAAA\n' + cert_bundle.PEM_END + b'\n'])


def test_append_pem_blocks_separates_blocks_with_a_blank_line():
    assert cert_bundle._append_pem_blocks(b'base', [b'one\n', b'two\n']) == b'base\n\none\n\ntwo\n'
    assert cert_bundle._append_pem_blocks(b'', [b'one\n']) == b'\none\n'


def test_atomic_write_replaces_the_file_and_leaves_no_temp(tmp_path):
    target = tmp_path / 'sub' / 'bundle.pem'
    cert_bundle.atomic_write(target, b'old')
    cert_bundle.atomic_write(target, b'new')
    assert [p.name for p in target.parent.iterdir()] == ['bundle.pem']
    assert target.read_bytes() == b'new'


def test_load_dir_cert_blocks_handles_a_missing_or_empty_dir(tmp_path, monkeypatch):
    monkeypatch.setattr(cert_bundle, 'HERE', tmp_path)
    assert cert_bundle.load_dir_cert_blocks('nope', '*.pem') == []
    (tmp_path / 'pins').mkdir()
    assert cert_bundle.load_dir_cert_blocks('pins', '*.pem') == []
    _, pem = make_cert()
    (tmp_path / 'pins' / 'a.pem').write_bytes(pem)
    ((path, block),) = cert_bundle.load_dir_cert_blocks('pins', '*.pem')
    assert block == pem
    assert path.name == 'a.pem'


# --- audit: deciding what to retire and what to rotate ---


def classify(bundle, pinned=None, required=(), *, probe_ok=True, warn_days=90):
    """classify_bundle over (subject, cert) pairs whose subject is the cert's CN, for readable cases."""
    pairs = [(c.subject.rfc4514_string(), c) for c in bundle]
    return cert_bundle.classify_bundle(
        pairs, pinned or {}, set(required), probe_ok=probe_ok, warn_days=warn_days, now=NOW
    )


def test_an_unused_pin_is_removable_when_every_probe_worked():
    pin, _ = make_cert('Pin')
    result = classify([pin], {'CN=Pin': 'pinned_certs/pin.pem'})
    assert result == cert_bundle.AuditResult(removable=['pinned_certs/pin.pem'], alert=False)


def test_an_unused_pin_is_kept_when_a_probe_failed():
    pin, _ = make_cert('Pin')
    assert classify([pin], {'CN=Pin': 'pinned_certs/pin.pem'}, probe_ok=False).removable == []


@pytest.mark.parametrize('days_left, alert', [(400, False), (30, True), (-1, True)])
def test_a_required_pin_alerts_inside_the_warning_window(days_left, alert):
    pin, _ = make_cert('Pin', days_left=days_left)
    result = classify([pin], {'CN=Pin': 'pinned_certs/pin.pem'}, required={'CN=Pin'})
    assert result == cert_bundle.AuditResult(removable=[], alert=alert)


@pytest.mark.parametrize('days_left, alert', [(400, False), (30, False), (-1, True)])
def test_a_base_root_is_never_removable_and_alerts_only_once_expired(days_left, alert):
    root, _ = make_cert('Base', days_left=days_left)
    assert classify([root]) == cert_bundle.AuditResult(removable=[], alert=alert)


# --- generate: the merged bundle, from a faked download ---


@pytest.fixture
def certs_dir(tmp_path, monkeypatch):
    """A stand-in certificates/ directory: generate() reads pinned_certs/ and writes the bundles here."""
    monkeypatch.setattr(cert_bundle, 'HERE', tmp_path)
    (tmp_path / cert_bundle.PINNED_CERTS_DIR).mkdir()
    return tmp_path


def downloader(pem: bytes, digest: str | None = None):
    published = (digest or hashlib.sha256(pem).hexdigest()) + '  cacert.pem\n'
    files = {cert_bundle.URL_PEM: pem, cert_bundle.URL_SHA256: published.encode()}
    return files.__getitem__


def test_generate_writes_the_verbatim_base_and_the_merged_bundle(certs_dir):
    _, base_a = make_cert('Base A')
    _, base_b = make_cert('Base B')
    curl = b'## curl header\n\nBase A\n=====\n' + base_a + b'\nBase B\n=====\n' + base_b
    _, pin = make_cert('Pin')
    (certs_dir / 'pinned_certs' / 'pin.pem').write_bytes(pin)
    # A pin that duplicates a base root is dropped, not shipped twice.
    (certs_dir / 'pinned_certs' / 'dup.pem').write_bytes(base_a)

    assert cert_bundle.generate(download=downloader(curl)) == 0

    assert (certs_dir / cert_bundle.BASE_PEM_FILE).read_bytes() == curl
    merged = (certs_dir / cert_bundle.MERGED_PEM_FILE).read_bytes()
    assert merged.startswith(cert_bundle.MERGED_BANNER)
    assert b'## curl header' not in merged
    # The base's blocks, then the one new pin: the duplicate is gone.
    blocks = [b.strip() for b in cert_bundle.extract_pem_cert_blocks(merged, source='merged')]
    assert blocks == [base_a.strip(), base_b.strip(), pin.strip()]


def test_generate_refuses_a_download_that_does_not_match_its_digest(certs_dir):
    _, base = make_cert('Base')
    with pytest.raises(cert_bundle.InputError, match='SHA-256 mismatch'):
        cert_bundle.generate(download=downloader(base, digest='0' * 64))
    assert not (certs_dir / cert_bundle.MERGED_PEM_FILE).exists()


def test_generate_refuses_to_ship_an_expired_pin(certs_dir):
    _, base = make_cert('Base')
    _, dead = make_cert('Dead', days_left=-1)
    (certs_dir / 'pinned_certs' / 'dead.pem').write_bytes(dead)
    with pytest.raises(cert_bundle.InputError, match='Refusing to ship 1 expired pinned'):
        cert_bundle.generate(download=downloader(base))


def test_generate_refuses_two_certs_with_one_subject(certs_dir):
    _, base = make_cert('Same')
    _, pin = make_cert('Same')  # a different key, so a different cert, under the same name
    (certs_dir / 'pinned_certs' / 'same.pem').write_bytes(pin)
    with pytest.raises(cert_bundle.InputError, match='Duplicate certificate subject'):
        cert_bundle.generate(download=downloader(base))
