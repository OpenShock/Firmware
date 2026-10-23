"""Tests for the offline parts of certificates/cert_bundle.py: PEM loading and validation, expiry, merging."""

import datetime

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
    certs = [make_cert(f'C{days}', days_left=days)[0] for days in (-5, 30, 400)]
    expired, expiring = cert_bundle.check_certs_expiry(certs, ['a', 'b', 'c'], warn_days=90)
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
    ca, _ = make_cert('CA')
    leaf, _ = make_cert('Leaf', ca=False)
    cert_bundle._reject_non_ca([ca], ['ca.pem'], kind='pinned')
    with pytest.raises(cert_bundle.InputError, match='non-CA pinned'):
        cert_bundle._reject_non_ca([ca, leaf], ['ca.pem', 'leaf.pem'], kind='pinned')


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
    assert cert_bundle.load_dir_cert_blocks('nope', '*.pem') == ([], [])
    (tmp_path / 'pins').mkdir()
    assert cert_bundle.load_dir_cert_blocks('pins', '*.pem') == ([], [])
    _, pem = make_cert()
    (tmp_path / 'pins' / 'a.pem').write_bytes(pem)
    blocks, paths = cert_bundle.load_dir_cert_blocks('pins', '*.pem')
    assert blocks == [pem]
    assert [p.name for p in paths] == ['a.pem']
