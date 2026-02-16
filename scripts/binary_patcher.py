#!/usr/bin/env python3
"""
ESP Firmware Binary Patcher

Patches ESP32/ESP8266 firmware binaries by replacing template content
while maintaining integrity checksums (XOR and SHA256).
"""

import sys
import struct
import hashlib
from pathlib import Path
from functools import reduce
from operator import xor
from itertools import chain
from typing import List

# Constants
APP_DESCRIPTOR_SIZE = 24
DIGEST_SIZE = 32
CHECKSUM_SIZE = 1

MAGIC = 0xE9
XOR_INIT = 0xEF


def main():
    """Main entry point for the binary patcher."""
    if len(sys.argv) != 4:
        print("Usage: binary_patcher.py <binary> <template_file> <replacement_file>", file=sys.stderr)
        sys.exit(1)

    binary_path = Path(sys.argv[1])
    template_path = Path(sys.argv[2])
    replacement_path = Path(sys.argv[3])

    # Validate input files exist
    if not binary_path.exists():
        print(f"Error: Binary file not found: {binary_path}", file=sys.stderr)
        sys.exit(1)
    if not template_path.exists():
        print(f"Error: Template file not found: {template_path}", file=sys.stderr)
        sys.exit(1)
    if not replacement_path.exists():
        print(f"Error: Replacement file not found: {replacement_path}", file=sys.stderr)
        sys.exit(1)

    try:
        patch_binary(binary_path, template_path, replacement_path)
        print("✓ Patching completed successfully!")
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


def patch_binary(binary_path: Path, template_path: Path, replacement_path: Path):
    """
    Patch a binary file by replacing template content and updating checksums.
    
    Args:
        binary_path: Path to the binary file to patch
        template_path: Path to the template file to search for
        replacement_path: Path to the replacement content
    """
    with binary_path.open("r+b") as f:
        file_bytes = bytearray(f.read())

        file_len = len(file_bytes)
        if file_len == 0:
            raise RuntimeError("Firmware binary empty!")

        # Calculate minimum required size
        min_size = APP_DESCRIPTOR_SIZE + DIGEST_SIZE + CHECKSUM_SIZE
        if file_len < min_size:
            raise RuntimeError(f"Firmware binary too small ({file_len} bytes, bare minimum {min_size} bytes required)!")

        print(f"Binary size: {file_len} bytes")
        
        validate_old_hash(file_bytes)
        segments = get_segments(file_bytes, print_segments=True)
        validate_old_checksum(file_bytes, segments)

        # --- Replace template in binary ---
        replace_template_in_binary(file_bytes, template_path, replacement_path)
        
        # Recalculate segments after modification
        segments = get_segments(file_bytes, print_segments=False)

        # --- Update XOR checksum ---
        new_xor = esp_xor_sum(segments)
        xor_offset = file_len - (DIGEST_SIZE + CHECKSUM_SIZE)
        file_bytes[xor_offset] = new_xor
        print(f"New XOR checksum: 0x{new_xor:02X}")

        # --- Update SHA256 digest ---
        digest_data = file_bytes[:file_len - DIGEST_SIZE]
        digest = hashlib.sha256(digest_data).digest()
        print(f"New SHA256 digest: {digest.hex().upper()}")
        file_bytes[file_len - DIGEST_SIZE:] = digest

        # Write back to file
        f.seek(0)
        f.write(file_bytes)
        f.truncate()  # Ensure file size is correct
        f.flush()


def replace_template_in_binary(file_bytes: bytearray, template_path: Path, replacement_path: Path):
    """
    Replace template content in binary with replacement content.
    
    Args:
        file_bytes: Binary data as bytearray
        template_path: Path to template file
        replacement_path: Path to replacement file
    """
    # Load template and replacement
    template_bytes = template_path.read_bytes()
    replacement_bytes = replacement_path.read_bytes()

    if len(template_bytes) == 0:
        raise RuntimeError("Template file is empty!")

    # Find template in binary
    start = file_bytes.find(template_bytes)
    if start == -1:
        raise RuntimeError(f"Template not found in binary!")

    # Ensure replacement fits
    if len(replacement_bytes) > len(template_bytes):
        raise RuntimeError(
            f"Replacement is too large: {len(replacement_bytes)} bytes "
            f"(template size: {len(template_bytes)} bytes)"
        )

    # Write replacement into binary and pad remaining space with zeros
    end_replacement = start + len(replacement_bytes)
    end_template = start + len(template_bytes)
    
    file_bytes[start:end_replacement] = replacement_bytes
    
    remaining = len(template_bytes) - len(replacement_bytes)
    if remaining > 0:
        file_bytes[end_replacement:end_template] = b'\x00' * remaining
        print(f"Replaced template at offset 0x{start:X} with {len(replacement_bytes)} bytes ({remaining} bytes zero-padded)")
    else:
        print(f"Replaced template at offset 0x{start:X} with {len(replacement_bytes)} bytes")


def validate_old_hash(file_bytes: bytes):
    """
    Validate the existing SHA256 hash in the binary.
    
    Args:
        file_bytes: Binary data
        
    Raises:
        RuntimeError: If hash validation fails
    """
    file_len = len(file_bytes)
    old_digest = file_bytes[file_len - DIGEST_SIZE:]

    print(f"Old SHA256 digest: {bytes(old_digest).hex().upper()}")

    # Validate SHA256
    computed_digest = hashlib.sha256(file_bytes[:file_len - DIGEST_SIZE]).digest()
    if old_digest != computed_digest:
        print(f"Expected digest: {computed_digest.hex().upper()}", file=sys.stderr)
        raise RuntimeError("Existing SHA256 digest mismatch - binary may be corrupted!")
    
    print("✓ Validated existing SHA256")


def validate_old_checksum(file_bytes: bytes, segments: List[bytes]):
    """
    Validate the existing XOR checksum in the binary.
    
    Args:
        file_bytes: Binary data
        segments: List of binary segments
        
    Raises:
        RuntimeError: If checksum validation fails
    """
    file_len = len(file_bytes)
    xor_offset = file_len - (DIGEST_SIZE + CHECKSUM_SIZE)
    old_xor = file_bytes[xor_offset]

    print(f"Old XOR checksum: 0x{old_xor:02X}")

    # Validate XOR
    computed_xor = esp_xor_sum(segments)
    if old_xor != computed_xor:
        print(f"Expected XOR: 0x{computed_xor:02X}", file=sys.stderr)
        raise RuntimeError("Existing XOR checksum mismatch - binary may be corrupted!")
    
    print("✓ Validated existing XOR checksum")  # Fixed typo: "existsing" -> "existing"


def get_segments(file_bytes: bytes, print_segments: bool) -> List[bytes]:
    """
    Extract segments from ESP firmware binary.
    
    Args:
        file_bytes: Binary data
        
    Returns:
        List of binary segments
        
    Raises:
        RuntimeError: If binary format is invalid
    """
    if file_bytes[0] != MAGIC:
        raise RuntimeError(
            f"Invalid magic byte: expected 0x{MAGIC:02X}, got 0x{file_bytes[0]:02X}"
        )

    segments = []
    segment_count = file_bytes[1]
    
    if segment_count == 0 or segment_count > 16:
        raise RuntimeError(f"Unreasonable segment count: {segment_count} (expected 1-16)")

    if print_segments:
        print(f"Found {segment_count} segment(s)")

    cursor = APP_DESCRIPTOR_SIZE

    for i in range(segment_count):
        # Ensure we don't read past end of file
        if cursor + 8 > len(file_bytes):
            raise RuntimeError(
                f"Segment {i}: Insufficient data for segment header at offset 0x{cursor:X}"
            )

        # Skip load address (u32)
        load_addr = struct.unpack_from("<I", file_bytes, cursor)[0]
        cursor += 4

        # Segment size (u32, little endian)
        seg_size = struct.unpack_from("<I", file_bytes, cursor)[0]
        cursor += 4

        # Validate segment size
        if seg_size == 0:
            raise RuntimeError(f"Segment {i}: Invalid segment size (0 bytes)")
        if cursor + seg_size > len(file_bytes):
            raise RuntimeError(
                f"Segment {i}: Segment extends past end of file "
                f"(offset: 0x{cursor:X}, size: {seg_size}, file size: {len(file_bytes)})"
            )

        segment_data = file_bytes[cursor:cursor + seg_size]
        segments.append(segment_data)
        
        if print_segments:
            print(f"  Segment {i}: load_addr=0x{load_addr:08X}, size={seg_size} bytes")
        cursor += seg_size

    return segments


def esp_xor_sum(segments: List[bytes]) -> int:
    """
    Calculate ESP-style XOR checksum across all segments.
    
    Args:
        segments: List of binary segments
        
    Returns:
        XOR checksum value (0-255)
    """
    return reduce(xor, chain.from_iterable(segments), XOR_INIT)


if __name__ == "__main__":
    main()