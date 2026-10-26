#include "www/WwwImage.h"

#include <algorithm>
#include <array>
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
  std::size_t padded(std::size_t size)
  {
    return size + ((Www::kAlign - (size % Www::kAlign)) % Www::kAlign);
  }

  struct RawEntry {
    uint16_t pathOff;
    uint16_t flags;
    uint32_t offset;
    uint32_t size;
  };

  RawEntry rawEntry(std::span<const uint8_t> bytes, std::size_t index)
  {
    const uint8_t* p = bytes.data() + Www::kHeaderSize + Www::kEntrySize * index;
    return {readU16(p), readU16(p + 2), readU32(p + 4), readU32(p + 8)};
  }

  // The path of entry `index`; the directory was validated by Parse, so this cannot run off the table.
  std::string_view entryPath(std::span<const uint8_t> bytes, uint16_t count, uint32_t dirLen, std::size_t index)
  {
    const std::size_t stringsStart = Www::kHeaderSize + Www::kEntrySize * count;
    const std::size_t stringsEnd   = Www::kHeaderSize + dirLen;
    const RawEntry entry           = rawEntry(bytes, index);
    const char* start              = reinterpret_cast<const char*>(bytes.data() + stringsStart + entry.pathOff);
    const char* end                = reinterpret_cast<const char*>(bytes.data() + stringsEnd);
    const char* nul                = static_cast<const char*>(memchr(start, '\0', end - start));
    return std::string_view(start, nul - start);
  }
}  // namespace

const char* Www::ParseErrorName(ParseError error)
{
  switch (error) {
    case ParseError::TooShort:
      return "too short";
    case ParseError::BadMagic:
      return "bad magic";
    case ParseError::BadVersion:
      return "unsupported version";
    case ParseError::BadDirectory:
      return "directory out of range";
    case ParseError::BadPath:
      return "path out of range";
    case ParseError::NotSorted:
      return "paths not sorted";
    case ParseError::BadFlags:
      return "unknown flags";
    case ParseError::BadBody:
      return "body out of range";
    case ParseError::HashMismatch:
      return "body hash mismatch";
    case ParseError::HashFailed:
      return "hashing failed";
  }
  return "unknown";
}

Www::Image::Image(std::span<const uint8_t> bytes, uint16_t count, uint32_t dirLen, std::size_t size)
  : m_bytes(bytes)
  , m_count(count)
  , m_dirLen(dirLen)
  , m_size(size)
{
}

std::expected<Www::Image, Www::ParseError> Www::Image::Parse(std::span<const uint8_t> bytes, Sha256Fn sha256)
{
  if (bytes.size() < kHeaderSize) {
    return std::unexpected(ParseError::TooShort);
  }
  if (readU32(bytes.data()) != kMagic) {
    return std::unexpected(ParseError::BadMagic);
  }
  if (readU16(bytes.data() + 4) != kVersion) {
    return std::unexpected(ParseError::BadVersion);
  }
  const uint16_t count  = readU16(bytes.data() + 6);
  const uint32_t dirLen = readU32(bytes.data() + 8);
  if (dirLen % kAlign != 0 || dirLen < kEntrySize * count || dirLen > bytes.size() - kHeaderSize) {
    return std::unexpected(ParseError::BadDirectory);
  }

  const std::size_t stringsStart = kHeaderSize + kEntrySize * count;
  const std::size_t stringsLen   = kHeaderSize + dirLen - stringsStart;
  const std::size_t bodyStart    = kHeaderSize + dirLen;
  std::size_t end                = bodyStart;
  std::string_view previous;

  for (std::size_t i = 0; i < count; ++i) {
    const RawEntry entry = rawEntry(bytes, i);
    if (entry.pathOff >= stringsLen) {
      return std::unexpected(ParseError::BadPath);
    }
    const uint8_t* pathStart = bytes.data() + stringsStart + entry.pathOff;
    const void* nul          = memchr(pathStart, '\0', stringsLen - entry.pathOff);
    if (nul == nullptr) {
      return std::unexpected(ParseError::BadPath);
    }
    const std::string_view path(reinterpret_cast<const char*>(pathStart), static_cast<const uint8_t*>(nul) - pathStart);
    if (i > 0 && path <= previous) {
      return std::unexpected(ParseError::NotSorted);
    }
    previous = path;
    if ((entry.flags & ~kKnownFlags) != 0) {
      return std::unexpected(ParseError::BadFlags);
    }
    // Checked against the span (not the image size we have not finished computing) with overflow-safe arithmetic.
    if (entry.offset < bodyStart || entry.offset % kAlign != 0 || entry.size > bytes.size() || entry.offset > bytes.size() - entry.size) {
      return std::unexpected(ParseError::BadBody);
    }
    end = std::max(end, static_cast<std::size_t>(entry.offset) + padded(entry.size));
  }
  if (end > bytes.size()) {
    return std::unexpected(ParseError::BadBody);  // a body's padding runs past the end
  }

  std::array<uint8_t, 32> digest {};
  if (!sha256(bytes.subspan(kHeaderSize, end - kHeaderSize), digest)) {
    return std::unexpected(ParseError::HashFailed);
  }
  if (memcmp(digest.data(), bytes.data() + 12, kBodyHashSize) != 0) {
    return std::unexpected(ParseError::HashMismatch);
  }

  return Image(bytes, count, dirLen, end);
}

Www::Asset Www::Image::At(std::size_t index) const
{
  const RawEntry entry = rawEntry(m_bytes, index);
  return Asset {
    .path  = entryPath(m_bytes, m_count, m_dirLen, index),
    .body  = m_bytes.subspan(entry.offset, entry.size),
    .flags = entry.flags,
  };
}

std::optional<Www::Asset> Www::Image::Find(std::string_view path) const
{
  std::size_t lo = 0, hi = m_count;
  while (lo < hi) {
    const std::size_t mid     = lo + (hi - lo) / 2;
    const std::string_view at = entryPath(m_bytes, m_count, m_dirLen, mid);
    if (at == path) {
      return At(mid);
    }
    if (at < path) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return std::nullopt;
}
