#pragma once

// The `www` image: the captive-portal assets as one flat, read-only directory.
//
// Written by scripts/utils/wwwimage.py (the one place the format is documented in full; keep both in
// step and bump kVersion together). Little-endian, everything 4-byte aligned:
//
//   header (32 B): magic u32 'OSWW', version u16, count u16, dir_len u32, body_hash u8[16], reserved u8[4]
//   entry (16 B) x count, sorted by path: path_off u16, flags u16, offset u32, size u32, reserved u32
//   string table: NUL-terminated paths, no leading slash
//   bodies: in entry order, each padded to 4 bytes
//
// body_hash is the truncated SHA-256 of the whole image (header included, with body_hash itself zeroed), so no
// byte the parser reads goes unchecked; reserved fields must be zero. Parse() checks every field before the image
// is used: a failure means "no portal assets", never a partially trusted image. The image is smaller
// than the partition it sits in; the bytes after it are ignored.
//
// Pure C++ over a byte span, so the host tests cover it; the device side (WwwPartition.h) maps the
// partition and hands the span in.

#include "util/FunctionRef.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace OpenShock::Www {
  inline constexpr uint32_t kMagic        = 0x5757534Fu;  // 'OSWW' as little-endian bytes
  inline constexpr uint16_t kVersion      = 1;
  inline constexpr std::size_t kHeaderSize   = 32;
  inline constexpr std::size_t kEntrySize    = 16;
  inline constexpr std::size_t kBodyHashSize = 16;
  inline constexpr std::size_t kAlign        = 4;

  inline constexpr uint16_t kFlagGzip      = 1u << 0;
  inline constexpr uint16_t kFlagImmutable = 1u << 1;  // content-addressed name: may be cached for a long time
  inline constexpr uint16_t kKnownFlags    = kFlagGzip | kFlagImmutable;

  struct Asset {
    std::string_view path;         // as stored: no leading slash
    std::span<const uint8_t> body;  // points into the image
    uint16_t flags;

    [[nodiscard]] bool gzip() const { return (flags & kFlagGzip) != 0; }
    [[nodiscard]] bool immutable() const { return (flags & kFlagImmutable) != 0; }
  };

  enum class ParseError : uint8_t {
    TooShort,
    BadMagic,
    BadVersion,
    BadDirectory,  // dir_len unaligned, too small for the entries, or past the end
    BadPath,       // path offset outside the string table, or unterminated
    NotSorted,     // paths not strictly increasing
    BadFlags,
    BadBody,       // body before the directory, unaligned, or past the end
    BadReserved,   // a reserved field of the header or an entry is not zero
    HashMismatch,
    HashFailed,  // the hasher itself failed
  };
  [[nodiscard]] const char* ParseErrorName(ParseError error);

  /// @brief SHA-256 of `parts`, concatenated, into `digest`; false when hashing is unavailable. Injected so the
  ///        parser stays pure.
  using Sha256Fn = Util::FunctionRef<bool(std::span<const std::span<const uint8_t>> parts, std::span<uint8_t, 32> digest)>;

  class Image {
  public:
    /// @brief Validates the image at the start of `bytes` (the whole mapped partition is fine) and verifies the body hash.
    [[nodiscard]] static std::expected<Image, ParseError> Parse(std::span<const uint8_t> bytes, Sha256Fn sha256);

    /// @brief The asset stored under `path` (no leading slash). Binary search over the sorted directory.
    [[nodiscard]] std::optional<Asset> Find(std::string_view path) const;

    [[nodiscard]] std::size_t Count() const { return m_count; }
    [[nodiscard]] Asset At(std::size_t index) const;

    /// @brief Bytes from the header to the end of the last body's padding: what an OTA hash of the image covers.
    [[nodiscard]] std::size_t ImageSize() const { return m_size; }
    /// @brief The image's own (truncated) hash; it changes whenever any byte of the image does.
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
