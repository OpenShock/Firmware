#include "partition_layout/PartitionTable.h"

#include "Md5.h"

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

const PartitionTable::Entry* PartitionTable::Table::Find(uint8_t type, uint8_t subtype, std::string_view label) const
{
  for (const Entry& entry : Entries()) {
    if (entry.type == type && (subtype == 0xFF || entry.subtype == subtype) && entry.Label() == label) {
      return &entry;
    }
  }
  return nullptr;
}

const PartitionTable::Entry* PartitionTable::Table::FindAppAt(uint32_t offset) const
{
  for (const Entry& entry : Entries()) {
    if (entry.IsApp() && entry.offset == offset) {
      return &entry;
    }
  }
  return nullptr;
}

uint32_t PartitionTable::Table::End() const
{
  uint32_t end = 0;
  for (const Entry& entry : Entries()) {
    end = std::max(end, entry.End());
  }
  return end;
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
  }
  return "unknown";
}

std::expected<PartitionTable::Table, PartitionTable::ParseError> PartitionTable::Parse(std::span<const uint8_t> bytes)
{
  if (bytes.size() < kEntrySize) {
    return std::unexpected(ParseError::TooShort);
  }
  const std::size_t limit = std::min(bytes.size(), kMaxLen);

  Table table {};
  std::size_t pos = 0;
  bool md5Seen    = false;
  for (; pos + kEntrySize <= limit; pos += kEntrySize) {
    const uint8_t* p     = bytes.data() + pos;
    const uint16_t magic = readU16(p);
    if (magic == kMagic) {
      if (md5Seen) {
        return std::unexpected(ParseError::Trailing);
      }
      if (table.count >= kMaxEntries) {
        return std::unexpected(ParseError::TooManyEntries);
      }
      Entry& entry  = table.entries[table.count++];
      entry.type    = p[2];
      entry.subtype = p[3];
      entry.offset  = readU32(p + 4);
      entry.size    = readU32(p + 8);
      memcpy(entry.label, p + 12, kLabelSize);
      entry.label[kLabelSize] = '\0';
      entry.flags             = readU32(p + 28);
      continue;
    }
    if (magic == kMd5Magic) {
      if (md5Seen) {
        return std::unexpected(ParseError::Trailing);
      }
      uint8_t digest[16];
      Internal::Md5(bytes.data(), pos, digest);
      if (memcmp(digest, p + 16, 16) != 0) {
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

  if (table.count == 0) {
    return std::unexpected(ParseError::NoEntries);
  }
  if (!md5Seen) {
    return std::unexpected(ParseError::NoMd5);
  }
  return table;
}

const char* PartitionTable::LayoutName(Layout layout)
{
  switch (layout) {
    case Layout::Unknown:
      return "unknown";
    case Layout::V1:
      return "v1 (1.x: littlefs config/static0)";
    case Layout::V2:
      return "v2 (2.x: cfg/www)";
  }
  return "unknown";
}

PartitionTable::Layout PartitionTable::Detect(const Table& table)
{
  const bool cfg     = table.Find(kTypeData, kSubtypeDataNvs, "cfg") != nullptr;
  const bool www     = table.Find(kTypeWww, 0xFF, "www") != nullptr;
  const bool config  = table.Find(kTypeData, kSubtypeDataSpiffs, "config") != nullptr;
  const bool static0 = table.Find(kTypeData, kSubtypeDataSpiffs, "static0") != nullptr;
  if (cfg && www && !config && !static0) {
    return Layout::V2;
  }
  if (config && static0 && !cfg && !www) {
    return Layout::V1;
  }
  return Layout::Unknown;
}

const char* PartitionTable::ViolationName(Violation violation)
{
  switch (violation) {
    case Violation::NotV2Layout:
      return "staged table is not a 2.x layout";
    case Violation::TooManyEntries:
      return "too many entries";
    case Violation::SizeUnaligned:
      return "an entry's size is not a positive multiple of 4 KiB";
    case Violation::AppUnaligned:
      return "an app entry is not 64 KiB aligned";
    case Violation::Overlap:
      return "entries overlap";
    case Violation::PastFlash:
      return "an entry ends past the flash";
    case Violation::NvsMoved:
      return "nvs moved";
    case Violation::OtadataMoved:
      return "otadata moved";
    case Violation::NoCfg:
      return "no cfg partition";
    case Violation::NoWww:
      return "no www partition";
    case Violation::GeometryChanged:
      return "an entry was added, removed, moved or resized";
    case Violation::EntryRetyped:
      return "an entry other than config and static0 changed type, subtype or label";
    case Violation::NoRunningSlot:
      return "no slot for the running image";
    case Violation::BootloaderHeaderUnknown:
      return "bootloader header flash size unknown";
    case Violation::BootloaderHeaderTooSmall:
      return "bootloader header declares a smaller flash than the table needs";
    case Violation::LiveUnreadable:
      return "live table unreadable";
    case Violation::LiveNotMigratable:
      return "live table is not a layout this firmware migrates from";
  }
  return "unknown";
}

std::expected<void, PartitionTable::Violation> PartitionTable::Validate(const Table& staged, const Context& context)
{
  const auto entries = staged.Entries();
  if (entries.size() > kMaxEntries) {
    return std::unexpected(Violation::TooManyEntries);
  }

  // 6. Geometry: what the bootloader checks, plus overlap.
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const Entry& entry = entries[i];
    if (entry.size == 0 || entry.size % kSectorSize != 0 || entry.offset % kSectorSize != 0) {
      return std::unexpected(Violation::SizeUnaligned);
    }
    if (entry.IsApp() && entry.offset % kAppAlignment != 0) {
      return std::unexpected(Violation::AppUnaligned);
    }
    if (entry.End() < entry.offset || entry.End() > context.flashSize) {
      return std::unexpected(Violation::PastFlash);
    }
    for (std::size_t j = i + 1; j < entries.size(); ++j) {
      if (entry.Overlaps(entries[j])) {
        return std::unexpected(Violation::Overlap);
      }
    }
  }

  // 3. The shared nvs and otadata never move: the bootloader's OTA state and the PHY cache stay where they are.
  const Entry* nvs = staged.Find(kTypeData, kSubtypeDataNvs, "nvs");
  if (nvs == nullptr || nvs->offset != kNvsOffset || nvs->size != kNvsSize) {
    return std::unexpected(Violation::NvsMoved);
  }
  const Entry* otadata = staged.Find(kTypeData, kSubtypeDataOta, "otadata");
  if (otadata == nullptr || otadata->offset != kOtadataOffset || otadata->size != kOtadataSize) {
    return std::unexpected(Violation::OtadataMoved);
  }

  // 5. The 2.x partitions the firmware hardcodes.
  if (staged.Find(kTypeData, kSubtypeDataNvs, "cfg") == nullptr) {
    return std::unexpected(Violation::NoCfg);
  }
  if (staged.Find(kTypeWww, 0xFF, "www") == nullptr) {
    return std::unexpected(Violation::NoWww);
  }
  if (Detect(staged) != Layout::V2) {
    return std::unexpected(Violation::NotV2Layout);
  }

  // 8. The live table: readable, and the layout this firmware knows how to leave.
  if (context.live == nullptr) {
    return std::unexpected(Violation::LiveUnreadable);
  }
  if (Detect(*context.live) != Layout::V1) {
    return std::unexpected(Violation::LiveNotMigratable);
  }

  // 9. Nothing moves: every staged entry sits exactly where a live one does (no overlap and the same count make that
  //    one to one), and only config and static0 become something else. No data of the old layout ends up inside a
  //    partition of another kind except the two the migration fills itself.
  const auto liveEntries = context.live->Entries();
  if (entries.size() != liveEntries.size()) {
    return std::unexpected(Violation::GeometryChanged);
  }
  for (const Entry& entry : entries) {
    const auto old = std::find_if(liveEntries.begin(), liveEntries.end(), [&](const Entry& e) { return entry.SamePlace(e); });
    if (old == liveEntries.end()) {
      return std::unexpected(Violation::GeometryChanged);
    }
    // cfg and www themselves (type and subtype) were checked above
    const std::string_view expected = old->Label() == "config" ? "cfg" : old->Label() == "static0" ? "www" : old->Label();
    const bool retyped              = expected != old->Label() ? entry.Label() != expected : (entry.type != old->type || entry.subtype != old->subtype || entry.Label() != expected);
    if (retyped) {
      return std::unexpected(Violation::EntryRetyped);
    }
  }

  // 4. Where the running image lives after the rewrite: its slot, grown in place, or an app slot the old table
  //    never used (so a copy of the image into it touches nothing the old layout cares about).
  bool runningSlotOk = false;
  if (const Entry* slot = staged.FindAppAt(context.runningOffset); slot != nullptr && slot->size >= context.runningImageSize) {
    runningSlotOk = true;
  } else {
    for (const Entry& entry : entries) {
      if (!entry.IsApp() || entry.size < context.runningImageSize) {
        continue;
      }
      bool clear = true;
      for (const Entry& old : context.live->Entries()) {
        if (entry.Overlaps(old)) {
          clear = false;
          break;
        }
      }
      if (clear) {
        runningSlotOk = true;
        break;
      }
    }
  }
  if (!runningSlotOk) {
    return std::unexpected(Violation::NoRunningSlot);
  }

  // 7. The bootloader sizes the flash from its own image header and rejects any entry past that size.
  if (context.bootloaderFlashSize == 0) {
    return std::unexpected(Violation::BootloaderHeaderUnknown);
  }
  if (staged.End() > context.bootloaderFlashSize) {
    return std::unexpected(Violation::BootloaderHeaderTooSmall);
  }

  return {};
}
