#pragma once

// The `www` image: the captive-portal assets as one read-only directory tree, every body gzipped.
//
// Written by scripts/utils/wwwimage.py (the one place the format is documented in full; keep both in
// step and bump kVersion together). Little-endian, everything 4-byte aligned:
//
//   header (64 B): magic u32 'OSWW', version u16, node_count u16, dir_len u32, body_hash u8[32], reserved u8[20]
//   node (16 B) x node_count: name_off u16, flags u16, offset u32, size u32, reserved u32
//     flags: bit0 directory, bit1 immutable (files only)
//     directory: offset = index of its first child, size = number of children
//     file: offset = body offset from the start of the image, size = body bytes
//   string table: NUL-terminated names, one path component each (no '/'), no ASCII uppercase
//   bodies: in node order, each padded to 4 bytes
//
// Node 0 is the root directory (empty name). A directory's children are one contiguous run of nodes, strictly
// sorted by name, and the runs are laid out breadth-first: walking the nodes in order, each directory's run starts
// where the previous one ended, the first right after the root. Every body is gzipped; there is no flag for it.
// Names are lowercase: the packer folds ASCII letters and Find() folds the request path the same way, so URL case
// never matters.
//
// body_hash is the SHA-256 of the whole image (header included, with body_hash itself zeroed), so no byte the
// parser reads goes unchecked; reserved fields must be zero. Parse() checks every field before the image
// is used: a failure means "no portal assets", never a partially trusted image. The image is smaller
// than the partition it sits in; the bytes after it are ignored.
//
// C++ over a byte span plus OpenShock::SHA256 (Hashing.h; the host tests stub it), so the host tests cover it; the
// device side (WwwPartition.h) maps the partition and hands the span in.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace OpenShock::Www {
  inline constexpr uint32_t kMagic           = 0x5757534Fu;  // 'OSWW' as little-endian bytes
  inline constexpr uint16_t kVersion         = 1;
  inline constexpr std::size_t kHeaderSize   = 64;
  inline constexpr std::size_t kNodeSize     = 16;
  inline constexpr std::size_t kBodyHashSize = 32;
  inline constexpr std::size_t kAlign        = 4;

  inline constexpr uint16_t kFlagDirectory = 1u << 0;
  inline constexpr uint16_t kFlagImmutable = 1u << 1;  // content-addressed name: may be cached for a long time
  inline constexpr uint16_t kKnownFlags    = kFlagDirectory | kFlagImmutable;

  // One entry of the node table, as stored.
  struct Node {
    std::string_view name;  // one path component; empty for the root
    uint16_t flags;
    uint32_t offset;        // directory: index of its first child; file: body offset from the start of the image
    uint32_t size;          // directory: number of children; file: body bytes

    [[nodiscard]] bool isDirectory() const { return (flags & kFlagDirectory) != 0; }
    [[nodiscard]] bool immutable() const { return (flags & kFlagImmutable) != 0; }
  };

  // A file found by path.
  struct Asset {
    std::string_view name;          // the last path component
    std::span<const uint8_t> body;  // gzipped; points into the image
    uint16_t flags;

    [[nodiscard]] bool immutable() const { return (flags & kFlagImmutable) != 0; }
  };

  enum class ParseError : uint8_t {
    TooShort,
    BadMagic,
    BadVersion,
    BadDirectory,  // dir_len unaligned, too small for the nodes, or past the end
    BadName,       // name offset outside the string table, unterminated, empty, or containing '/'
    NotSorted,     // a directory's children not strictly increasing
    BadFlags,
    BadBody,       // body before the directory, unaligned, or past the end
    BadTree,       // no root, a non-directory root, or a child run that is not the next one in order
    BadReserved,   // a reserved field of the header or a node is not zero
    HashMismatch,
    HashFailed,    // the hasher itself failed
  };
  [[nodiscard]] const char* ParseErrorName(ParseError error);

  class Image {
  public:
    /// @brief Validates the image at the start of `bytes` (the whole mapped partition is fine) and verifies its hash.
    [[nodiscard]] static std::expected<Image, ParseError> Parse(std::span<const uint8_t> bytes);

    /// @brief The file at `path` ('/'-separated, no leading slash). A binary search over each directory on the way.
    [[nodiscard]] std::optional<Asset> Find(std::string_view path) const;

    /// @brief Number of nodes, the root and directories included.
    [[nodiscard]] std::size_t Count() const { return m_count; }
    [[nodiscard]] Node At(std::size_t index) const;

    /// @brief Bytes from the header to the end of the last body's padding: what an OTA hash of the image covers.
    [[nodiscard]] std::size_t ImageSize() const { return m_size; }
    /// @brief The image's own hash; it changes whenever any byte of the image does.
    [[nodiscard]] std::span<const uint8_t, kBodyHashSize> BodyHash() const { return m_bytes.subspan<12, kBodyHashSize>(); }
    [[nodiscard]] std::span<const uint8_t> Bytes() const { return m_bytes.first(m_size); }

  private:
    Image(std::span<const uint8_t> bytes, uint16_t count, uint32_t dirLen, std::size_t size);

    std::span<const uint8_t> m_bytes;
    uint16_t m_count;
    uint32_t m_dirLen;
    std::size_t m_size;
  };
}  // namespace OpenShock::Www
