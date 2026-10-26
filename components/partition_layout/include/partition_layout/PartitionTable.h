#pragma once

// The binary partition table the bootloader reads at 0x8000 (components/bootloader_support/include/
// esp_flash_partitions.h): 32-byte entries - magic 0x50AA, type, subtype, offset u32, size u32, label[16],
// flags u32 - followed by an MD5 entry (magic 0xEBEB, 14 bytes 0xFF, the MD5 of everything before it) and
// 0xFF up to 0xC00. scripts/utils/partitions.py writes it; this reads it.
//
// Layouts (docs/STORAGE.md):
//   V1  the 1.x table: littlefs `config` and `static0` data/spiffs partitions, 1,664 KiB app slots.
//   V2  the 2.x tables: a `cfg` data/nvs partition and a `www` partition of type 0x40.
//
// Validate() is the gate in front of the one dangerous write the firmware ever makes: it refuses anything
// that the bootloader would refuse, anything that would move what the running image depends on, and anything
// that would strand the device. Every check is listed in the design (OpenShock Firmware 2.0 storage and
// partition migration, "invariants"); the Violation enum names them one by one.

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
  inline constexpr uint8_t kTypeWww           = 0x40;  // OpenShock's own: the flat asset image
  inline constexpr uint8_t kSubtypeDataOta    = 0x00;
  inline constexpr uint8_t kSubtypeDataNvs    = 0x02;
  inline constexpr uint8_t kSubtypeDataSpiffs = 0x82;
  inline constexpr uint32_t kAppAlignment     = 0x10000;  // app partitions start on a 64 KiB boundary
  inline constexpr uint32_t kSectorSize       = 0x1000;

  // What every OpenShock layout keeps in place: the migration invariant.
  inline constexpr uint32_t kNvsOffset     = 0x9000;
  inline constexpr uint32_t kNvsSize       = 0x5000;
  inline constexpr uint32_t kOtadataOffset = 0xE000;
  inline constexpr uint32_t kOtadataSize   = 0x2000;
  inline constexpr uint32_t kApp0Offset    = 0x10000;

  struct Entry {
    uint8_t type;
    uint8_t subtype;
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
    char label[kLabelSize + 1];  // NUL-terminated

    [[nodiscard]] std::string_view Label() const { return label; }
    [[nodiscard]] bool IsApp() const { return type == kTypeApp; }
    [[nodiscard]] uint32_t End() const { return offset + size; }
    [[nodiscard]] bool Overlaps(const Entry& other) const { return offset < other.End() && other.offset < End(); }
    [[nodiscard]] bool SamePlace(const Entry& other) const { return offset == other.offset && size == other.size; }
  };

  struct Table {
    std::array<Entry, kMaxEntries> entries;
    std::size_t count;

    [[nodiscard]] std::span<const Entry> Entries() const { return std::span<const Entry>(entries).first(count); }
    [[nodiscard]] const Entry* Find(std::string_view label) const;
    /// @brief The entry of `type` (and `subtype`, unless 0xFF) labelled `label`.
    [[nodiscard]] const Entry* Find(uint8_t type, uint8_t subtype, std::string_view label) const;
    [[nodiscard]] const Entry* FindAppAt(uint32_t offset) const;
    /// @brief One past the last byte any entry uses.
    [[nodiscard]] uint32_t End() const;
  };

  enum class ParseError : uint8_t {
    TooShort,
    NoEntries,
    TooManyEntries,
    NoMd5,        // the table ended without an MD5 entry: the bootloader refuses it
    Md5Mismatch,  // the MD5 entry does not match the entries
    Trailing,     // something other than 0xFF after the MD5 entry
  };
  [[nodiscard]] const char* ParseErrorName(ParseError error);

  /// @brief Decodes `bytes` (the first 0xC00 of a table; longer is fine) and verifies its MD5.
  [[nodiscard]] std::expected<Table, ParseError> Parse(std::span<const uint8_t> bytes);

  enum class Layout : uint8_t {
    Unknown,
    V1,  // 1.x: littlefs config + static0
    V2,  // 2.x: cfg + www
  };
  [[nodiscard]] const char* LayoutName(Layout layout);
  [[nodiscard]] Layout Detect(const Table& table);

  struct Context {
    uint32_t flashSize;            // the chip's real size
    uint32_t bootloaderFlashSize;  // what the bootloader image header at the bootloader offset declares; 0 when unreadable
    uint32_t runningOffset;        // the slot the migration runs from
    uint32_t runningImageSize;     // bytes the running image occupies in it
    const Table* live;             // the table at 0x8000 right now; null when it could not be read
  };

  enum class Violation : uint8_t {
    NotV2Layout,               // the staged table is not a 2.x layout at all
    TooManyEntries,            //
    SizeUnaligned,             // an entry's size is 0 or not a multiple of 4 KiB
    AppUnaligned,              // an app entry does not start on a 64 KiB boundary
    Overlap,                   // two entries overlap
    PastFlash,                 // an entry ends past the chip
    NvsMoved,                  // the shared nvs is not at 0x9000 / 0x5000
    OtadataMoved,              // otadata is not at 0xE000 / 0x2000
    NoCfg,                     // no data/nvs entry labelled cfg
    NoWww,                     // no type-0x40 entry labelled www
    NoRunningSlot,             // no app entry at the running offset large enough, and no app entry clear of the live table
    BootloaderHeaderUnknown,   // the bootloader's declared flash size could not be read
    BootloaderHeaderTooSmall,  // the bootloader declares a flash smaller than the table needs: it would reject the table
    LiveUnreadable,            // the live table could not be parsed
    LiveNotMigratable,         // the live table is not the layout this firmware migrates from
  };
  [[nodiscard]] const char* ViolationName(Violation violation);

  /// @brief Every invariant the design lists, in order; the first one that fails is reported and nothing else is
  ///        assumed about the table. A staged table that passes may be written over the live one.
  [[nodiscard]] std::expected<void, Violation> Validate(const Table& staged, const Context& context);
}  // namespace OpenShock::PartitionTable
