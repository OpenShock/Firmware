#include "sdcard/SdLayout.h"

#include "Checksum.h"

#include <algorithm>
#include <array>
#include <cstring>

using namespace OpenShock::Diagnostics::SdCard;

static constexpr std::size_t kCrcCoverage   = kSectorSize - sizeof(uint32_t);  // Every sector CRC covers 0x000 to 0x1FB
static constexpr uint32_t kProbeCount       = 64;
static constexpr std::size_t kMaxTagLen     = 32;
static constexpr std::size_t kMinCutMessage = 32;  // Below this a cut message goes to a fresh block instead

uint32_t OpenShock::Diagnostics::SdCard::SectorCrc(const void* sector)
{
  return OpenShock::Checksum::Crc32(std::span<const uint8_t>(static_cast<const uint8_t*>(sector), kCrcCoverage));
}

template<typename T>
static void sealSector(T& sector)
{
  sector.crc32 = SectorCrc(&sector);
}

template<std::size_t N>
static void copyString(char (&dst)[N], std::string_view src)
{
  std::memset(dst, 0, N);
  std::memcpy(dst, src.data(), std::min(src.size(), N - 1));
}

std::optional<Geometry> OpenShock::Diagnostics::SdCard::PlanGeometry(uint32_t cardSectors, uint32_t epoch)
{
  if (cardSectors < kMinCardSectors) {
    return std::nullopt;
  }

  uint32_t logSectors = cardSectors - kLogPartitionLba;
  logSectors -= logSectors % kAlignSectors;

  return Geometry {
    .cardSectors = cardSectors,
    .epoch       = epoch,
    .logSectors  = logSectors,
    .ringBlocks  = (logSectors - kRingDataStart) & ~7u,
  };
}

// ---------------------------------------------------------------------------------------------------------------------
// Format and classification

static MbrPartitionEntry makeEntry(uint8_t type, uint32_t start, uint32_t sectors)
{
  return MbrPartitionEntry {
    .status   = 0x00,
    .chsFirst = {0xFE, 0xFF, 0xFF},
    .type     = type,
    .chsLast  = {0xFE, 0xFF, 0xFF},
    .lbaStart = start,
    .sectors  = sectors,
  };
}

static Superblock makeSuperblock(const FormatInfo& info, const Geometry& geometry, bool log)
{
  Superblock sb {};
  std::memcpy(sb.magic, log ? kLogSuperblockMagic : kDumpSuperblockMagic, sizeof(sb.magic));
  sb.version          = kLayoutVersion;
  sb.flags            = 0;
  sb.epoch            = geometry.epoch;
  sb.unitSectors      = log ? 1 : kSlotSectors;
  sb.dataStart        = log ? kRingDataStart : kDumpDataStart;
  sb.unitCount        = log ? geometry.ringBlocks : kSlotCount;
  sb.partitionLba     = log ? kLogPartitionLba : kDumpPartitionLba;
  sb.partitionSectors = log ? geometry.logSectors : kDumpPartitionSectors;
  sb.cardSectors      = geometry.cardSectors;
  sb.formatUnixS      = info.formatUnixS;
  std::memcpy(sb.mac, info.mac, sizeof(sb.mac));
  copyString(sb.fwVersion, info.fwVersion);
  copyString(sb.board, info.board);
  sealSector(sb);
  return sb;
}

static bool writeVerified(BlockDevice& device, uint32_t lba, const void* data)
{
  std::array<uint8_t, kSectorSize> readBack;
  return device.WriteSector(lba, static_cast<const uint8_t*>(data)) && device.ReadSector(lba, readBack.data()) && std::memcmp(readBack.data(), data, kSectorSize) == 0;
}

bool OpenShock::Diagnostics::SdCard::Format(BlockDevice& device, const FormatInfo& info)
{
  if (info.epoch == 0 || info.epoch == UINT32_MAX) {
    return false;
  }

  auto geometry = PlanGeometry(device.SectorCount(), info.epoch);
  if (!geometry) {
    return false;
  }

  // Reserved sectors: D+2..D+2,047 and L+2..L+7.
  std::array<uint8_t, kSectorSize> zero {};
  for (uint32_t lba = kDumpPartitionLba + 2; lba < kDumpPartitionLba + kDumpDataStart; lba++) {
    if (!writeVerified(device, lba, zero.data())) {
      return false;
    }
  }
  for (uint32_t lba = kLogPartitionLba + 2; lba < kLogPartitionLba + kRingDataStart; lba++) {
    if (!writeVerified(device, lba, zero.data())) {
      return false;
    }
  }

  // Backup before primary, dump before log, MBR last: an interrupted format leaves no valid MBR, or superblock copies
  // that disagree, and the card classifies as foreign.
  Superblock dumpSb = makeSuperblock(info, *geometry, false);
  Superblock logSb  = makeSuperblock(info, *geometry, true);
  if (!writeVerified(device, kDumpPartitionLba + 1, &dumpSb) || !writeVerified(device, kDumpPartitionLba, &dumpSb) || !writeVerified(device, kLogPartitionLba + 1, &logSb) || !writeVerified(device, kLogPartitionLba, &logSb)) {
    return false;
  }

  std::array<uint8_t, kSectorSize> mbr {};
  uint32_t signature = info.diskSignature;
  std::memcpy(mbr.data() + kMbrSignatureOffset, &signature, sizeof(signature));
  MbrPartitionEntry entries[2] = {
    makeEntry(kDumpPartitionType, kDumpPartitionLba, kDumpPartitionSectors),
    makeEntry(kLogPartitionType, kLogPartitionLba, geometry->logSectors),
  };
  std::memcpy(mbr.data() + kMbrEntriesOffset, entries, sizeof(entries));
  mbr[kMbrBootSigOffset]     = 0x55;
  mbr[kMbrBootSigOffset + 1] = 0xAA;

  return writeVerified(device, 0, mbr.data());
}

enum class ReadResult : uint8_t {
  Valid,
  Invalid,
  IoError,
};

static ReadResult readSuperblock(BlockDevice& device, uint32_t lba, const char (&magic)[8], const MbrPartitionEntry& entry, Superblock& sb)
{
  if (!device.ReadSector(lba, reinterpret_cast<uint8_t*>(&sb))) {
    return ReadResult::IoError;
  }

  bool valid = std::memcmp(sb.magic, magic, sizeof(sb.magic)) == 0 && sb.version == kLayoutVersion && sb.crc32 == SectorCrc(&sb) && sb.partitionLba == entry.lbaStart && sb.partitionSectors == entry.sectors && sb.cardSectors == device.SectorCount();
  return valid ? ReadResult::Valid : ReadResult::Invalid;
}

// Both copies of a partition's superblock: identical if both are valid, otherwise whichever one is.
static ReadResult readSuperblockPair(BlockDevice& device, const char (&magic)[8], const MbrPartitionEntry& entry, Superblock& out)
{
  Superblock primary, backup;
  ReadResult a = readSuperblock(device, entry.lbaStart, magic, entry, primary);
  ReadResult b = readSuperblock(device, entry.lbaStart + 1, magic, entry, backup);
  if (a == ReadResult::IoError || b == ReadResult::IoError) {
    return ReadResult::IoError;
  }

  if (a == ReadResult::Valid && b == ReadResult::Valid) {
    if (std::memcmp(&primary, &backup, sizeof(Superblock)) != 0) {
      return ReadResult::Invalid;  // An interrupted format
    }
    out = primary;
    return ReadResult::Valid;
  }
  if (a == ReadResult::Valid) {
    out = primary;
    return ReadResult::Valid;
  }
  if (b == ReadResult::Valid) {
    out = backup;
    return ReadResult::Valid;
  }
  return ReadResult::Invalid;
}

Classification OpenShock::Diagnostics::SdCard::Classify(BlockDevice& device)
{
  Classification result {CardClass::Foreign, {}};

  std::array<uint8_t, kSectorSize> mbr;
  if (!device.ReadSector(0, mbr.data())) {
    result.cardClass = CardClass::IoError;
    return result;
  }

  if (std::all_of(mbr.begin(), mbr.end(), [](uint8_t b) { return b == 0x00; }) || std::all_of(mbr.begin(), mbr.end(), [](uint8_t b) { return b == 0xFF; })) {
    result.cardClass = CardClass::Blank;
    return result;
  }

  if (mbr[kMbrBootSigOffset] != 0x55 || mbr[kMbrBootSigOffset + 1] != 0xAA) {
    return result;
  }

  MbrPartitionEntry entries[4];
  std::memcpy(entries, mbr.data() + kMbrEntriesOffset, sizeof(entries));
  const MbrPartitionEntry& dump = entries[0];
  const MbrPartitionEntry& log  = entries[1];

  auto expected = PlanGeometry(device.SectorCount(), 0);
  if (!expected) {
    return result;
  }
  if (dump.type != kDumpPartitionType || dump.lbaStart != kDumpPartitionLba || dump.sectors != kDumpPartitionSectors) {
    return result;
  }
  if (log.type != kLogPartitionType || log.lbaStart != kLogPartitionLba || log.sectors == 0 || static_cast<uint64_t>(log.lbaStart) + log.sectors > device.SectorCount()) {
    return result;
  }
  const uint8_t* unused = mbr.data() + kMbrEntriesOffset + 2 * sizeof(MbrPartitionEntry);
  if (std::any_of(unused, unused + 2 * sizeof(MbrPartitionEntry), [](uint8_t b) { return b != 0; })) {
    return result;
  }

  Superblock dumpSb, logSb;
  ReadResult dumpRead = readSuperblockPair(device, kDumpSuperblockMagic, dump, dumpSb);
  ReadResult logRead  = readSuperblockPair(device, kLogSuperblockMagic, log, logSb);
  if (dumpRead == ReadResult::IoError || logRead == ReadResult::IoError) {
    result.cardClass = CardClass::IoError;
    return result;
  }
  if (dumpRead != ReadResult::Valid || logRead != ReadResult::Valid || dumpSb.epoch != logSb.epoch || logSb.epoch == 0 || logSb.epoch == UINT32_MAX) {
    return result;
  }

  // The unit fields have exactly one valid value each in this version; anything else is a layout this firmware doesn't
  // know, and it never writes to one of those.
  uint32_t ringBlocks = (log.sectors - kRingDataStart) & ~7u;
  if (dumpSb.unitSectors != kSlotSectors || dumpSb.dataStart != kDumpDataStart || dumpSb.unitCount != kSlotCount || logSb.unitSectors != 1 || logSb.dataStart != kRingDataStart || logSb.unitCount != ringBlocks || ringBlocks == 0) {
    return result;
  }

  result.cardClass = CardClass::Ours;
  result.geometry  = Geometry {
     .cardSectors = device.SectorCount(),
     .epoch       = logSb.epoch,
     .logSectors  = log.sectors,
     .ringBlocks  = ringBlocks,
  };
  return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ring head recovery

static ReadResult readBlock(BlockDevice& device, const Geometry& geometry, uint32_t position, LogBlock& block)
{
  if (!device.ReadSector(geometry.RingLba(position), reinterpret_cast<uint8_t*>(&block))) {
    return ReadResult::IoError;
  }

  bool valid = block.magic == kBlockMagic && block.epoch == geometry.epoch && block.crc32 == SectorCrc(&block) && block.payloadLen <= kBlockPayloadSize && block.seq % geometry.ringBlocks == position;
  return valid ? ReadResult::Valid : ReadResult::Invalid;
}

std::optional<RingHead> OpenShock::Diagnostics::SdCard::RecoverHead(BlockDevice& device, const Geometry& geometry, Util::FunctionRef<void()> onFullScan)
{
  const uint32_t n = geometry.ringBlocks;
  LogBlock block;

  // lap(p): seq / n of a valid block, -1 for an invalid one. "lap(p) == K" holds for positions 0..head and fails
  // after them - the previous lap, never-written blocks and a torn block at head + 1 alike.
  bool ioError = false;
  auto lapAt   = [&](uint32_t position, LogBlock& out) -> int64_t {
    ReadResult r = readBlock(device, geometry, position, out);
    if (r == ReadResult::IoError) {
      ioError = true;
    }
    return r == ReadResult::Valid ? static_cast<int64_t>(out.seq / n) : -1;
  };

  bool empty    = false;
  bool tornZero = false;  // Position 0 invalid but n - 1 valid: torn at the start of a new lap
  uint32_t head = 0;
  int64_t lap   = -1;
  LogBlock headBlock {};

  int64_t lap0 = lapAt(0, block);
  if (ioError) {
    return std::nullopt;
  }
  if (lap0 >= 0) {
    lap         = lap0;
    headBlock   = block;
    uint32_t lo = 0, hi = n;  // lap(lo) == K; hi is past the end or known != K
    while (hi - lo > 1) {
      uint32_t mid = lo + (hi - lo) / 2;
      if (lapAt(mid, block) == lap) {
        lo        = mid;
        headBlock = block;
      } else {
        hi = mid;
      }
      if (ioError) {
        return std::nullopt;
      }
    }
    head = lo;
  } else {
    int64_t lapLast = lapAt(n - 1, block);
    if (ioError) {
      return std::nullopt;
    }
    if (lapLast >= 0) {
      lap       = lapLast;
      head      = n - 1;
      headBlock = block;
      tornZero  = true;
    } else {
      empty = true;
    }
  }

  // Verify: the block after the head must not continue its sequence, and 64 evenly spaced probes must agree.
  bool consistent = true;
  if (!empty) {
    uint32_t next = (head + 1) % n;
    if (next != head && lapAt(next, block) >= 0 && block.seq == headBlock.seq + 1) {
      consistent = false;
    }
  }
  for (uint32_t i = 1; consistent && i < kProbeCount; i++) {
    uint32_t position = static_cast<uint32_t>(static_cast<uint64_t>(i) * n / kProbeCount);
    if (position == 0 || (!empty && position == head)) {
      continue;
    }
    int64_t probe = lapAt(position, block);
    if (empty) {
      consistent = probe < 0;
    } else if (position <= head && !(tornZero && position == 0)) {
      consistent = probe == lap;
    } else {
      consistent = probe < 0 || probe == lap - 1;
    }
  }
  if (ioError) {
    return std::nullopt;
  }

  if (consistent) {
    if (empty) {
      return RingHead {.empty = true, .seq = 0, .boot = 0, .fullScan = false};
    }
    return RingHead {.empty = false, .seq = headBlock.seq, .boot = headBlock.boot, .fullScan = false};
  }

  // Damaged blocks in the middle of the ring: the head is the valid block with the highest seq.
  onFullScan();
  std::optional<LogBlock> best;
  for (uint32_t position = 0; position < n; position++) {
    if (lapAt(position, block) >= 0 && (!best || block.seq > best->seq)) {
      best = block;
    }
    if (ioError) {
      return std::nullopt;
    }
  }

  if (!best) {
    return RingHead {.empty = true, .seq = 0, .boot = 0, .fullScan = true};
  }
  return RingHead {.empty = false, .seq = best->seq, .boot = best->boot, .fullScan = true};
}

// ---------------------------------------------------------------------------------------------------------------------
// Ring writer

LogRing::LogRing(std::span<LogBlock> buffer) noexcept
  : m_blocks(buffer)
{
}

void LogRing::Open(BlockDevice& device, const Geometry& geometry, const RingHead& head, uint32_t boot)
{
  m_device   = &device;
  m_geometry = geometry;
  m_nextSeq  = head.empty ? 0 : head.seq + 1;
  m_boot     = boot;
  m_sealed   = 0;
  m_fill     = 0;
}

void LogRing::Close()
{
  m_device = nullptr;
  m_sealed = 0;
  m_fill   = 0;
}

uint32_t LogRing::deltaMs(uint64_t uptimeMs) const
{
  uint64_t base = m_blocks[m_sealed].uptimeMs;
  return uptimeMs > base ? static_cast<uint32_t>(std::min<uint64_t>(uptimeMs - base, UINT32_MAX)) : 0;
}

std::size_t LogRing::room(uint64_t uptimeMs, bool newBlock)
{
  if (newBlock && m_fill != 0) {
    m_blocks[m_sealed].payloadLen = static_cast<uint16_t>(m_fill);
    m_sealed++;
    m_fill = 0;
  }
  if (m_fill == 0) {
    if (m_sealed >= m_blocks.size()) {
      return 0;
    }
    std::memset(&m_blocks[m_sealed], 0, sizeof(LogBlock));
    m_blocks[m_sealed].uptimeMs = uptimeMs;
  }
  return kBlockPayloadSize - m_fill;
}

uint8_t* LogRing::reserve(RecordType type, uint8_t flags, std::size_t len)
{
  uint8_t* record = m_blocks[m_sealed].payload + m_fill;
  record[0]       = static_cast<uint8_t>(type);
  record[1]       = flags;
  uint16_t len16  = static_cast<uint16_t>(len);
  std::memcpy(record + 2, &len16, sizeof(len16));
  m_fill += kRecordHeaderSize + len;
  return record + kRecordHeaderSize;
}

bool LogRing::Add(RecordType type, std::span<const uint8_t> data, uint64_t uptimeMs, uint8_t flags)
{
  std::size_t need = kRecordHeaderSize + data.size();
  if (need > kBlockPayloadSize) {
    return true;  // Can never fit; the fixed-size record types are all far smaller
  }

  std::size_t space = room(uptimeMs, false);
  if (space < need) {
    space = room(uptimeMs, true);
  }
  if (space < need) {
    return false;
  }

  std::memcpy(reserve(type, flags, data.size()), data.data(), data.size());
  return true;
}

bool LogRing::AddLog(uint8_t level, std::string_view tag, std::string_view message, uint64_t uptimeMs)
{
  tag                    = tag.substr(0, kMaxTagLen);
  const std::size_t base = kRecordHeaderSize + 4 + 1 + 1 + tag.size();  // header, delta_ms, level, tag_len, tag
  const std::size_t full = base + message.size();

  uint8_t flags     = 0;
  std::size_t space = room(uptimeMs, false);
  if (space < full) {
    if (full <= kBlockPayloadSize) {
      space = room(uptimeMs, true);
    } else {
      // Too long for any block: cut to what is left here if that is worth it, otherwise to an empty block.
      if (space < base + kMinCutMessage) {
        space = room(uptimeMs, true);
      }
      flags = kRecordFlagTruncated;
    }
  }
  if (space < base) {
    return false;
  }

  std::size_t messageLen = std::min(message.size(), space - base);
  if (messageLen < message.size()) {
    // Cut on a UTF-8 character boundary
    while (messageLen > 0 && (static_cast<uint8_t>(message[messageLen]) & 0xC0) == 0x80) {
      messageLen--;
    }
    flags = kRecordFlagTruncated;
  }

  uint32_t delta = deltaMs(uptimeMs);
  uint8_t* data  = reserve(RecordType::Log, flags, base - kRecordHeaderSize + messageLen);
  std::memcpy(data, &delta, sizeof(delta));
  data[4] = level;
  data[5] = static_cast<uint8_t>(tag.size());
  std::memcpy(data + 6, tag.data(), tag.size());
  std::memcpy(data + 6 + tag.size(), message.data(), messageLen);
  return true;
}

bool LogRing::AddTimeSync(int64_t unixUs, TimeSource source, uint64_t uptimeMs)
{
  static constexpr std::size_t kLen = 16;
  std::size_t space                 = room(uptimeMs, false);
  if (space < kRecordHeaderSize + kLen) {
    space = room(uptimeMs, true);
  }
  if (space < kRecordHeaderSize + kLen) {
    return false;
  }

  uint32_t delta = deltaMs(uptimeMs);
  uint8_t* data  = reserve(RecordType::TimeSync, 0, kLen);
  std::memset(data, 0, kLen);
  std::memcpy(data, &delta, sizeof(delta));
  std::memcpy(data + 4, &unixUs, sizeof(unixUs));
  data[12] = static_cast<uint8_t>(source);
  return true;
}

bool LogRing::Flush()
{
  if (m_device == nullptr) {
    return false;
  }

  if (m_fill != 0) {
    m_blocks[m_sealed].payloadLen = static_cast<uint16_t>(m_fill);
    m_sealed++;
    m_fill = 0;
  }
  if (m_sealed == 0) {
    return true;
  }

  const uint32_t n = m_geometry.ringBlocks;
  for (uint32_t i = 0; i < m_sealed; i++) {
    LogBlock& block = m_blocks[i];
    block.magic     = kBlockMagic;
    block.epoch     = m_geometry.epoch;
    block.seq       = m_nextSeq + i;
    block.boot      = m_boot;
    block.flags     = 0;
    sealSector(block);
  }

  // One write, or two where the run crosses the end of the ring.
  uint32_t position   = static_cast<uint32_t>(m_nextSeq % n);
  uint32_t first      = std::min(m_sealed, n - position);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(m_blocks.data());
  if (!m_device->WriteSectors(m_geometry.RingLba(position), data, first)) {
    return false;
  }
  if (first < m_sealed && !m_device->WriteSectors(m_geometry.RingLba(0), data + first * kSectorSize, m_sealed - first)) {
    return false;
  }

  m_nextSeq += m_sealed;
  m_sealed = 0;
  return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dump slots

bool DumpStore::Open(BlockDevice& device, const Geometry& geometry)
{
  m_device   = &device;
  m_geometry = geometry;
  m_head.reset();

  for (uint32_t slot = 0; slot < kSlotCount; slot++) {
    SlotHeader header;
    if (!device.ReadSector(Geometry::SlotLba(slot), reinterpret_cast<uint8_t*>(&header))) {
      m_device = nullptr;
      return false;
    }
    bool valid = header.magic == kSlotMagic && header.epoch == geometry.epoch && header.crc32 == SectorCrc(&header) && header.seq % kSlotCount == slot;
    if (valid && (!m_head || header.seq > m_head->seq)) {
      m_head = header;
    }
  }

  return true;
}

std::optional<uint32_t> DumpStore::Checksum(uint32_t size, ObjectReader reader, std::span<uint8_t> scratch)
{
  uint32_t crc = 0;
  for (uint32_t offset = 0; offset < size;) {
    std::size_t len = std::min<std::size_t>(scratch.size(), size - offset);
    if (!reader(offset, scratch.data(), len)) {
      return std::nullopt;
    }
    crc = OpenShock::Checksum::Crc32(scratch.first(len), crc);
    offset += static_cast<uint32_t>(len);
  }
  return crc;
}

std::optional<SlotHeader> DumpStore::Store(SlotHeader header, uint32_t size, ObjectReader reader, std::span<uint8_t> scratch)
{
  const std::size_t chunk = scratch.size() - scratch.size() % kSectorSize;
  if (m_device == nullptr || size > kSlotDataBytes || chunk == 0) {
    return std::nullopt;
  }

  const uint64_t seq = NextSeq();
  const uint32_t lba = Geometry::SlotLba(static_cast<uint32_t>(seq % kSlotCount));

  // A zeroed header first invalidates the object the slot held.
  std::memset(scratch.data(), 0, kSectorSize);
  if (!m_device->WriteSector(lba, scratch.data())) {
    return std::nullopt;
  }

  uint32_t crc = 0;
  for (uint32_t offset = 0; offset < size;) {
    std::size_t len     = std::min<std::size_t>(chunk, size - offset);
    std::size_t sectors = (len + kSectorSize - 1) / kSectorSize;
    std::memset(scratch.data(), 0, sectors * kSectorSize);
    if (!reader(offset, scratch.data(), len)) {
      return std::nullopt;
    }
    crc = OpenShock::Checksum::Crc32(scratch.first(len), crc);
    if (!m_device->WriteSectors(lba + 1 + offset / kSectorSize, scratch.data(), static_cast<uint32_t>(sectors))) {
      return std::nullopt;
    }
    offset += static_cast<uint32_t>(len);
  }

  header.magic     = kSlotMagic;
  header.epoch     = m_geometry.epoch;
  header.seq       = seq;
  header.size      = size;
  header.dataCrc32 = crc;
  std::memset(header.reserved0, 0, sizeof(header.reserved0));
  std::memset(header.reserved1, 0, sizeof(header.reserved1));
  sealSector(header);

  // The commit.
  if (!writeVerified(*m_device, lba, &header)) {
    return std::nullopt;
  }

  // Read the data back.
  auto readBack = [&](uint32_t offset, uint8_t* out, std::size_t len) {
    std::size_t sectors = (len + kSectorSize - 1) / kSectorSize;
    if (sectors * kSectorSize > scratch.size() || out != scratch.data()) {
      return false;
    }
    return m_device->ReadSectors(lba + 1 + offset / kSectorSize, out, static_cast<uint32_t>(sectors));
  };
  auto storedCrc = Checksum(size, readBack, scratch.first(chunk));
  if (!storedCrc || *storedCrc != crc) {
    return std::nullopt;
  }

  m_head = header;
  return header;
}
