#include "www/WwwImage.h"

#include "Hashing.h"

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
  constexpr std::size_t kHashOffset = 12;  // body_hash in the header

  std::size_t padded(std::size_t size)
  {
    return size + ((Www::kAlign - (size % Www::kAlign)) % Www::kAlign);
  }

  // Names are stored lowercase (ASCII); a request path is folded the same way while comparing, never copied.
  char foldAscii(char c)
  {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
  }
  bool isLowercase(std::string_view name)
  {
    for (char c : name) {
      if (c >= 'A' && c <= 'Z') return false;
    }
    return true;
  }
  // <0, 0, >0 as `name` compares to the folded `request`, bytewise
  int compareFolded(std::string_view name, std::string_view request)
  {
    const std::size_t n = std::min(name.size(), request.size());
    for (std::size_t i = 0; i < n; ++i) {
      const auto a = static_cast<unsigned char>(name[i]);
      const auto b = static_cast<unsigned char>(foldAscii(request[i]));
      if (a != b) return a < b ? -1 : 1;
    }
    if (name.size() == request.size()) return 0;
    return name.size() < request.size() ? -1 : 1;
  }

  struct RawNode {
    uint16_t nameOff;
    uint16_t flags;
    uint32_t offset;
    uint32_t size;
    uint32_t reserved;

    bool isDirectory() const { return (flags & Www::kFlagDirectory) != 0; }
  };

  RawNode rawNode(std::span<const uint8_t> bytes, std::size_t index)
  {
    const uint8_t* p = bytes.data() + Www::kHeaderSize + Www::kNodeSize * index;
    return {readU16(p), readU16(p + 2), readU32(p + 4), readU32(p + 8), readU32(p + 12)};
  }

  // The name of node `index`; the string table was validated by Parse, so this cannot run off the table.
  std::string_view nodeName(std::span<const uint8_t> bytes, uint16_t count, uint32_t dirLen, std::size_t index)
  {
    const std::size_t stringsStart = Www::kHeaderSize + Www::kNodeSize * count;
    const std::size_t stringsEnd   = Www::kHeaderSize + dirLen;
    const RawNode node             = rawNode(bytes, index);
    const char* start              = reinterpret_cast<const char*>(bytes.data() + stringsStart + node.nameOff);
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
    case ParseError::BadName:
      return "bad name";
    case ParseError::NotSorted:
      return "children not sorted";
    case ParseError::BadFlags:
      return "unknown flags";
    case ParseError::BadBody:
      return "body out of range";
    case ParseError::BadTree:
      return "bad directory tree";
    case ParseError::BadReserved:
      return "reserved field not zero";
    case ParseError::HashMismatch:
      return "image hash mismatch";
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

bool Www::StartsLikeImage(std::span<const uint8_t> bytes)
{
  return bytes.size() >= sizeof(kMagic) && readU32(bytes.data()) == kMagic;
}

std::expected<Www::Image, Www::ParseError> Www::Image::Parse(std::span<const uint8_t> bytes)
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
  if (dirLen % kAlign != 0 || dirLen < kNodeSize * count || dirLen > bytes.size() - kHeaderSize) {
    return std::unexpected(ParseError::BadDirectory);
  }
  for (std::size_t i = kHashOffset + kBodyHashSize; i < kHeaderSize; ++i) {
    if (bytes[i] != 0) {
      return std::unexpected(ParseError::BadReserved);
    }
  }
  if (count == 0) {
    return std::unexpected(ParseError::BadTree);  // no root
  }

  const std::size_t stringsStart = kHeaderSize + kNodeSize * count;
  const std::size_t stringsLen   = kHeaderSize + dirLen - stringsStart;
  const std::size_t bodyStart    = kHeaderSize + dirLen;
  std::size_t end                = bodyStart;

  // Pass 1: every node on its own. Child runs must follow each other in node order, starting right after the root.
  std::size_t nextRun = 1;
  for (std::size_t i = 0; i < count; ++i) {
    const RawNode node = rawNode(bytes, i);
    if (node.nameOff >= stringsLen) {
      return std::unexpected(ParseError::BadName);
    }
    const uint8_t* nameStart = bytes.data() + stringsStart + node.nameOff;
    const void* nul          = memchr(nameStart, '\0', stringsLen - node.nameOff);
    if (nul == nullptr) {
      return std::unexpected(ParseError::BadName);
    }
    const std::string_view name(reinterpret_cast<const char*>(nameStart), static_cast<const uint8_t*>(nul) - nameStart);
    if (i == 0) {
      if (!name.empty() || !node.isDirectory()) {
        return std::unexpected(ParseError::BadTree);
      }
    } else if (name.empty() || name.find('/') != std::string_view::npos || name == "." || name == ".." || !isLowercase(name)) {
      return std::unexpected(ParseError::BadName);
    }
    if ((node.flags & ~kKnownFlags) != 0) {
      return std::unexpected(ParseError::BadFlags);
    }
    if (node.reserved != 0) {
      return std::unexpected(ParseError::BadReserved);
    }
    if (node.isDirectory()) {
      if ((node.flags & kFlagImmutable) != 0) {
        return std::unexpected(ParseError::BadFlags);
      }
      if (node.offset != nextRun || node.size > count - nextRun) {
        return std::unexpected(ParseError::BadTree);
      }
      nextRun += node.size;
    } else {
      // Checked against the span (not the image size we have not finished computing) with overflow-safe arithmetic.
      if (node.offset < bodyStart || node.offset % kAlign != 0 || node.size > bytes.size() || node.offset > bytes.size() - node.size) {
        return std::unexpected(ParseError::BadBody);
      }
      end = std::max(end, static_cast<std::size_t>(node.offset) + padded(node.size));
    }
  }
  if (nextRun != count) {
    return std::unexpected(ParseError::BadTree);  // nodes no directory reaches
  }
  if (end > bytes.size()) {
    return std::unexpected(ParseError::BadBody);  // a body's padding runs past the end
  }

  // Pass 2: each directory's children strictly increasing by name (every name is valid now)
  for (std::size_t i = 0; i < count; ++i) {
    const RawNode node = rawNode(bytes, i);
    if (!node.isDirectory()) {
      continue;
    }
    for (std::size_t j = node.offset + 1; j < node.offset + node.size; ++j) {
      if (nodeName(bytes, count, dirLen, j - 1) >= nodeName(bytes, count, dirLen, j)) {
        return std::unexpected(ParseError::NotSorted);
      }
    }
  }

  // The image as it was hashed: body_hash zeroed
  static constexpr uint8_t kZeroHash[kBodyHashSize] = {};
  SHA256 sha;
  SHA256::Digest digest {};
  const bool hashed = sha.begin() && sha.update(bytes.data(), kHashOffset) && sha.update(kZeroHash, kBodyHashSize) && sha.update(bytes.data() + kHashOffset + kBodyHashSize, end - kHashOffset - kBodyHashSize) && sha.finish(digest);
  if (!hashed) {
    return std::unexpected(ParseError::HashFailed);
  }
  if (memcmp(digest.data(), bytes.data() + kHashOffset, kBodyHashSize) != 0) {
    return std::unexpected(ParseError::HashMismatch);
  }

  return Image(bytes, count, dirLen, end);
}

Www::Node Www::Image::At(std::size_t index) const
{
  const RawNode node = rawNode(m_bytes, index);
  return Node {
    .name   = nodeName(m_bytes, m_count, m_dirLen, index),
    .flags  = node.flags,
    .offset = node.offset,
    .size   = node.size,
  };
}

std::optional<Www::Asset> Www::Image::Find(std::string_view path) const
{
  RawNode dir = rawNode(m_bytes, 0);
  for (;;) {
    const std::size_t slash          = path.find('/');
    const std::string_view component = path.substr(0, slash);
    if (component.empty()) {
      return std::nullopt;  // "", a leading or trailing slash, or "//"
    }

    std::size_t lo = dir.offset, hi = dir.offset + dir.size;
    std::size_t found = std::string_view::npos;
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      const int cmp         = compareFolded(nodeName(m_bytes, m_count, m_dirLen, mid), component);
      if (cmp == 0) {
        found = mid;
        break;
      }
      if (cmp < 0) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (found == std::string_view::npos) {
      return std::nullopt;
    }

    const RawNode node = rawNode(m_bytes, found);
    if (slash == std::string_view::npos) {
      if (node.isDirectory()) {
        return std::nullopt;
      }
      return Asset {
        .name  = nodeName(m_bytes, m_count, m_dirLen, found),
        .body  = m_bytes.subspan(node.offset, node.size),
        .flags = node.flags,
      };
    }
    if (!node.isDirectory()) {
      return std::nullopt;
    }
    dir = node;
    path.remove_prefix(slash + 1);
  }
}
