#pragma once

// RFC 1321 MD5, for the partition table's MD5 entry. Kept inside this component so the table parser has no
// mbedTLS dependency and runs on the host unchanged. Not for anything security related; the bootloader uses
// the MD5 as a checksum and so does this.
#include <cstddef>
#include <cstdint>

namespace OpenShock::PartitionTable::Internal {
  void Md5(const uint8_t* data, std::size_t length, uint8_t (&digest)[16]);
}  // namespace OpenShock::PartitionTable::Internal
