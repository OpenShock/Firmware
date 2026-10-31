#pragma once

// The binary partition table the bootloader reads at 0x8000 (components/bootloader_support/include/
// esp_flash_partitions.h): 32-byte entries - magic 0x50AA, type, subtype, offset u32, size u32, label[16],
// flags u32 - followed by an MD5 entry (magic 0xEBEB, 14 bytes 0xFF, the MD5 of everything before it) and
// 0xFF up to 0xC00.
//
// What the in-place table migrations need (docs/STORAGE.md): read the table, and retype one entry - its type,
// subtype and label - without moving anything, re-sealing the MD5. Pure C++ over bytes plus OpenShock::MD5, so the
// host tests run it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace OpenShock::PartitionTable {
  inline constexpr std::size_t kMaxLen     = 0xC00;
  inline constexpr std::size_t kEntrySize  = 32;
  inline constexpr std::size_t kMaxEntries = kMaxLen / kEntrySize - 1;  // minus the MD5 entry
  inline constexpr std::size_t kLabelSize  = 16;
  inline constexpr uint16_t kMagic         = 0x50AA;
  inline constexpr uint16_t kMd5Magic      = 0xEBEB;

  inline constexpr uint8_t kTypeApp           = 0x00;
  inline constexpr uint8_t kTypeData          = 0x01;
  inline constexpr uint8_t kSubtypeDataNvs    = 0x02;
  inline constexpr uint8_t kSubtypeDataSpiffs = 0x82;

  /// @brief The raw table, as it is on flash.
  using Bytes = std::array<uint8_t, kMaxLen>;

  struct Entry {
    uint8_t type;
    uint8_t subtype;
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
    char label[kLabelSize + 1];  // NUL-terminated

    [[nodiscard]] std::string_view Label() const { return label; }
  };

  struct Table {
    std::array<Entry, kMaxEntries> entries;
    std::size_t count;

    [[nodiscard]] std::span<const Entry> Entries() const { return std::span<const Entry>(entries).first(count); }
    [[nodiscard]] const Entry* Find(std::string_view label) const;
  };

  enum class ParseError : uint8_t {
    TooShort,
    NoEntries,
    TooManyEntries,
    NoMd5,        // the table ended without an MD5 entry: the bootloader refuses it
    Md5Mismatch,  // the MD5 entry does not match the entries
    Trailing,     // something other than 0xFF after the MD5 entry
    HashFailed,   // computing the MD5 failed: nothing is known about the table
  };
  [[nodiscard]] const char* ParseErrorName(ParseError error);

  /// @brief Decodes `bytes` (the first 0xC00 of a table; longer is fine) into `out` and verifies its MD5. A Table is
  ///        some 3.4 KiB, so on the device `out` belongs on the heap, not on a task's stack.
  [[nodiscard]] std::expected<void, ParseError> Parse(std::span<const uint8_t> bytes, Table& out);

  enum class RetypeError : uint8_t {
    Unparsable,    // `bytes` is not a table that parses
    NoSuchEntry,   // no entry is labelled `label`
    LabelTooLong,  // the new label does not fit the 16-byte field
    LabelTaken,    // another entry already has the new label
    HashFailed,    // computing the MD5 failed
  };
  [[nodiscard]] const char* RetypeErrorName(RetypeError error);

  /// @brief Gives the entry labelled `label` a new type, subtype and label in place - offset, size and flags stay -
  ///        and re-seals the MD5. On failure `bytes` is unchanged.
  [[nodiscard]] std::expected<void, RetypeError> Retype(Bytes& bytes, std::string_view label, uint8_t type, uint8_t subtype, std::string_view newLabel);
}  // namespace OpenShock::PartitionTable
