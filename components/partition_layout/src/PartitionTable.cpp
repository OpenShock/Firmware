#include "partition_layout/PartitionTable.h"

#include "Hashing.h"

#include <algorithm>
#include <cstring>

using namespace OpenShock;

namespace {
  uint16_t readU16(const uint8_t* p)
  {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
  }
  uint32_t readU32(const uint8_t* p)
  {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  }

  std::string_view rawLabel(const uint8_t* entry)
  {
    const char* label = reinterpret_cast<const char*>(entry + 12);
    return std::string_view(label, strnlen(label, PartitionTable::kLabelSize));
  }

  // Checks the structure and the MD5 without decoding anything: the number of entries, or why the bootloader would
  // refuse the table.
  std::expected<std::size_t, PartitionTable::ParseError> scan(std::span<const uint8_t> bytes)
  {
    using namespace PartitionTable;

    if (bytes.size() < kEntrySize) {
      return std::unexpected(ParseError::TooShort);
    }
    const std::size_t limit = std::min(bytes.size(), kMaxLen);

    std::size_t count = 0;
    std::size_t pos   = 0;
    bool md5Seen      = false;
    for (; pos + kEntrySize <= limit; pos += kEntrySize) {
      const uint8_t* p     = bytes.data() + pos;
      const uint16_t magic = readU16(p);
      if (magic == kMagic) {
        if (md5Seen) {
          return std::unexpected(ParseError::Trailing);
        }
        if (count >= kMaxEntries) {
          return std::unexpected(ParseError::TooManyEntries);
        }
        ++count;
        continue;
      }
      if (magic == kMd5Magic) {
        if (md5Seen) {
          return std::unexpected(ParseError::Trailing);
        }
        MD5::Digest digest {};
        if (!MD5::compute(bytes.data(), pos, digest)) {
          return std::unexpected(ParseError::HashFailed);
        }
        if (memcmp(digest.data(), p + 16, digest.size()) != 0) {
          return std::unexpected(ParseError::Md5Mismatch);
        }
        md5Seen = true;
        continue;
      }
      // 0xFFFF (erased flash) or anything else: the end of the table. What follows must be erased.
      for (std::size_t i = pos; i < limit; ++i) {
        if (bytes[i] != 0xFF) {
          return std::unexpected(md5Seen ? ParseError::Trailing : ParseError::NoMd5);
        }
      }
      break;
    }

    if (count == 0) {
      return std::unexpected(ParseError::NoEntries);
    }
    if (!md5Seen) {
      return std::unexpected(ParseError::NoMd5);
    }
    return count;
  }
}  // namespace

const PartitionTable::Entry* PartitionTable::Table::Find(std::string_view label) const
{
  for (const Entry& entry : Entries()) {
    if (entry.Label() == label) {
      return &entry;
    }
  }
  return nullptr;
}

const char* PartitionTable::ParseErrorName(ParseError error)
{
  switch (error) {
    case ParseError::TooShort:
      return "too short";
    case ParseError::NoEntries:
      return "no entries";
    case ParseError::TooManyEntries:
      return "too many entries";
    case ParseError::NoMd5:
      return "no MD5 entry";
    case ParseError::Md5Mismatch:
      return "MD5 mismatch";
    case ParseError::Trailing:
      return "data after the MD5 entry";
    case ParseError::HashFailed:
      return "computing the MD5 failed";
  }
  return "unknown";
}

std::expected<void, PartitionTable::ParseError> PartitionTable::Parse(std::span<const uint8_t> bytes, Table& out)
{
  auto count = scan(bytes);
  if (!count) {
    return std::unexpected(count.error());
  }

  out.count = *count;
  for (std::size_t i = 0; i < out.count; ++i) {
    const uint8_t* p = bytes.data() + i * kEntrySize;
    Entry& entry     = out.entries[i];
    entry.type       = p[2];
    entry.subtype    = p[3];
    entry.offset     = readU32(p + 4);
    entry.size       = readU32(p + 8);
    memcpy(entry.label, p + 12, kLabelSize);
    entry.label[kLabelSize] = '\0';
    entry.flags             = readU32(p + 28);
  }
  return {};
}

const char* PartitionTable::RetypeErrorName(RetypeError error)
{
  switch (error) {
    case RetypeError::Unparsable:
      return "the table does not parse";
    case RetypeError::NoSuchEntry:
      return "no entry has that label";
    case RetypeError::LabelTooLong:
      return "the new label is too long";
    case RetypeError::LabelTaken:
      return "another entry has the new label";
    case RetypeError::HashFailed:
      return "computing the MD5 failed";
  }
  return "unknown";
}

std::expected<void, PartitionTable::RetypeError> PartitionTable::Retype(Bytes& bytes, std::string_view label, uint8_t type, uint8_t subtype, std::string_view newLabel)
{
  if (newLabel.size() > kLabelSize) {
    return std::unexpected(RetypeError::LabelTooLong);
  }
  auto count = scan(bytes);
  if (!count) {
    return std::unexpected(count.error() == ParseError::HashFailed ? RetypeError::HashFailed : RetypeError::Unparsable);
  }

  uint8_t* target = nullptr;
  for (std::size_t i = 0; i < *count; ++i) {
    uint8_t* p                 = bytes.data() + i * kEntrySize;
    const std::string_view own = rawLabel(p);
    if (own == label) {
      target = p;
    } else if (own == newLabel) {
      return std::unexpected(RetypeError::LabelTaken);
    }
  }
  if (target == nullptr) {
    return std::unexpected(RetypeError::NoSuchEntry);
  }

  uint8_t original[kEntrySize];
  memcpy(original, target, kEntrySize);

  target[2] = type;
  target[3] = subtype;
  memset(target + 12, 0, kLabelSize);
  memcpy(target + 12, newLabel.data(), newLabel.size());

  // The MD5 entry follows the last entry; its digest covers every entry before it
  const std::size_t md5Pos = *count * kEntrySize;
  MD5::Digest digest {};
  if (!MD5::compute(bytes.data(), md5Pos, digest)) {
    memcpy(target, original, kEntrySize);
    return std::unexpected(RetypeError::HashFailed);
  }
  memcpy(bytes.data() + md5Pos + 16, digest.data(), digest.size());
  return {};
}
