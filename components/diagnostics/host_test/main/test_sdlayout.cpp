#include "unity.h"

#include "Checksum.h"
#include "sdcard/SdLayout.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace OpenShock::Diagnostics::SdCard;

namespace {
  using Sector = std::array<uint8_t, kSectorSize>;

  // A sparse card: sectors never written read as `fill`.
  class MemoryDevice final : public BlockDevice {
  public:
    explicit MemoryDevice(uint32_t sectors, uint8_t fill = 0x00)
      : m_sectors(sectors)
      , m_fill(fill)
    {
    }

    uint32_t SectorCount() const override { return m_sectors; }

    bool ReadSector(uint32_t lba, uint8_t* out) override
    {
      reads++;
      if (lba >= m_sectors) {
        return false;
      }
      auto it = data.find(lba);
      if (it == data.end()) {
        std::memset(out, m_fill, kSectorSize);
      } else {
        std::memcpy(out, it->second.data(), kSectorSize);
      }
      return true;
    }

    bool WriteSector(uint32_t lba, const uint8_t* in) override
    {
      if (lba >= m_sectors || failWrites) {
        return false;
      }
      std::memcpy(data[lba].data(), in, kSectorSize);
      return true;
    }

    void Corrupt(uint32_t lba) { data[lba][100] ^= 0xFF; }

    std::map<uint32_t, Sector> data;
    uint64_t reads  = 0;
    bool failWrites = false;

  private:
    uint32_t m_sectors;
    uint8_t m_fill;
  };

  constexpr uint32_t kTestEpoch = 0x0BADC0DE;

  FormatInfo testFormatInfo(uint32_t epoch = kTestEpoch)
  {
    FormatInfo info {};
    info.epoch           = epoch;
    info.diskSignature   = 0x5EED1234;
    info.formatUnixS     = 1'790'000'000;
    const uint8_t mac[6] = {0x02, 0x00, 0x5E, 0x10, 0x20, 0x30};
    std::memcpy(info.mac, mac, sizeof(mac));
    info.fwVersion = "1.6.0-test";
    info.board     = "Host-Test-Board";
    return info;
  }

  // A small ring for the head-recovery and writer tests; the real card plan has millions of blocks.
  Geometry smallGeometry(uint32_t ringBlocks, uint32_t epoch = kTestEpoch)
  {
    return Geometry {.cardSectors = kMinCardSectors, .epoch = epoch, .logSectors = ringBlocks + kRingDataStart, .ringBlocks = ringBlocks};
  }

  RingHead recover(MemoryDevice& device, const Geometry& geometry, bool* scanned = nullptr)
  {
    bool fullScan = false;
    auto head     = RecoverHead(device, geometry, [&] { fullScan = true; });
    TEST_ASSERT_TRUE(head.has_value());
    TEST_ASSERT_EQUAL(fullScan, head->fullScan);
    if (scanned != nullptr) {
      *scanned = fullScan;
    }
    return *head;
  }

  // Writes `count` blocks of one LOG record each through LogRing.
  void writeBlocks(MemoryDevice& device, const Geometry& geometry, uint32_t count, uint32_t boot = 0)
  {
    std::array<LogBlock, 4> buffer;
    LogRing ring(buffer);
    ring.Open(device, geometry, recover(device, geometry), boot);
    for (uint32_t i = 0; i < count; i++) {
      TEST_ASSERT_TRUE(ring.AddLog(3, "test", "block", i));
      TEST_ASSERT_TRUE(ring.Flush());
    }
  }

  LogBlock readBlock(MemoryDevice& device, const Geometry& geometry, uint32_t position)
  {
    LogBlock block;
    TEST_ASSERT_TRUE(device.ReadSector(geometry.RingLba(position), reinterpret_cast<uint8_t*>(&block)));
    return block;
  }

  struct Record {
    RecordType type;
    uint8_t flags;
    std::vector<uint8_t> data;
  };

  std::vector<Record> parseRecords(const LogBlock& block)
  {
    std::vector<Record> records;
    std::size_t pos = 0;
    while (pos + kRecordHeaderSize <= block.payloadLen) {
      uint16_t len;
      std::memcpy(&len, block.payload + pos + 2, 2);
      auto type = static_cast<RecordType>(block.payload[pos]);
      if (type == RecordType::Invalid || type == RecordType::End) {
        break;
      }
      const uint8_t* data = block.payload + pos + kRecordHeaderSize;
      records.push_back(Record {type, block.payload[pos + 1], std::vector<uint8_t>(data, data + len)});
      pos += kRecordHeaderSize + len;
    }
    TEST_ASSERT_EQUAL(block.payloadLen, pos);
    return records;
  }

  std::string logMessage(const Record& record)
  {
    uint8_t tagLen = record.data[5];
    return std::string(record.data.begin() + 6 + tagLen, record.data.end());
  }
}  // namespace

TEST_CASE("Sector CRC is CRC-32/ISO-HDLC", "[sdlayout]")
{
  const char* check = "123456789";
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, OpenShock::Checksum::Crc32(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(check), 9)));
}

TEST_CASE("PlanGeometry refuses cards under 1 GiB and rounds the log partition to 4 MiB", "[sdlayout]")
{
  TEST_ASSERT_FALSE(PlanGeometry(kMinCardSectors - 1, 1).has_value());

  auto g = PlanGeometry(kMinCardSectors, 1);
  TEST_ASSERT_TRUE(g.has_value());
  TEST_ASSERT_EQUAL_UINT32(1'564'672, g->logSectors);
  TEST_ASSERT_EQUAL_UINT32(1'564'664, g->ringBlocks);

  auto odd = PlanGeometry(kMinCardSectors + 8191 + 5, 1);
  TEST_ASSERT_EQUAL_UINT32(g->logSectors + 8192, odd->logSectors);
  TEST_ASSERT_EQUAL_UINT32(0, odd->ringBlocks % 8);
  TEST_ASSERT_EQUAL_UINT32(532'480, kLogPartitionLba);
}

TEST_CASE("Blank cards and other people's cards are not ours", "[sdlayout]")
{
  MemoryDevice zeros(kMinCardSectors, 0x00);
  TEST_ASSERT_EQUAL(CardClass::Blank, Classify(zeros).cardClass);

  MemoryDevice ones(kMinCardSectors, 0xFF);
  TEST_ASSERT_EQUAL(CardClass::Blank, Classify(ones).cardClass);

  // A FAT32 card: one 0x0C partition
  MemoryDevice fat(kMinCardSectors);
  Sector mbr {};
  MbrPartitionEntry entry {
    0x00, {0xFE, 0xFF, 0xFF},
     0x0C, {0xFE, 0xFF, 0xFF},
     8192, kMinCardSectors - 8192
  };
  std::memcpy(mbr.data() + kMbrEntriesOffset, &entry, sizeof(entry));
  mbr[kMbrBootSigOffset]     = 0x55;
  mbr[kMbrBootSigOffset + 1] = 0xAA;
  fat.WriteSector(0, mbr.data());
  TEST_ASSERT_EQUAL(CardClass::Foreign, Classify(fat).cardClass);
}

TEST_CASE("Format writes an MBR and superblocks that classify as ours", "[sdlayout]")
{
  MemoryDevice card(kMinCardSectors + 12345);
  TEST_ASSERT_TRUE(Format(card, testFormatInfo()));

  auto cls = Classify(card);
  TEST_ASSERT_EQUAL(CardClass::Ours, cls.cardClass);
  TEST_ASSERT_EQUAL_HEX32(kTestEpoch, cls.geometry.epoch);
  TEST_ASSERT_EQUAL_UINT32((card.SectorCount() - kLogPartitionLba) / 8192 * 8192, cls.geometry.logSectors);

  Sector mbr;
  card.ReadSector(0, mbr.data());
  MbrPartitionEntry entries[2];
  std::memcpy(entries, mbr.data() + kMbrEntriesOffset, sizeof(entries));
  TEST_ASSERT_EQUAL_HEX8(0xFA, entries[0].type);
  TEST_ASSERT_EQUAL_UINT32(8192, entries[0].lbaStart);
  TEST_ASSERT_EQUAL_UINT32(524'288, entries[0].sectors);
  TEST_ASSERT_EQUAL_HEX8(0x7F, entries[1].type);
  TEST_ASSERT_EQUAL_UINT32(532'480, entries[1].lbaStart);

  // Identical primary and backup
  TEST_ASSERT_EQUAL_MEMORY(card.data[kLogPartitionLba].data(), card.data[kLogPartitionLba + 1].data(), kSectorSize);
  TEST_ASSERT_EQUAL_MEMORY(card.data[kDumpPartitionLba].data(), card.data[kDumpPartitionLba + 1].data(), kSectorSize);
  Superblock sb;
  std::memcpy(&sb, card.data[kLogPartitionLba].data(), sizeof(sb));
  TEST_ASSERT_EQUAL_STRING("OSLOGSB", sb.magic);
  TEST_ASSERT_EQUAL_STRING("Host-Test-Board", sb.board);
  TEST_ASSERT_EQUAL_UINT32(card.SectorCount(), sb.cardSectors);
}

TEST_CASE("Format refuses the epochs reserved as invalid and cards under 1 GiB", "[sdlayout]")
{
  MemoryDevice card(kMinCardSectors);
  TEST_ASSERT_FALSE(Format(card, testFormatInfo(0)));
  TEST_ASSERT_FALSE(Format(card, testFormatInfo(UINT32_MAX)));

  MemoryDevice small(kMinCardSectors - 1);
  TEST_ASSERT_FALSE(Format(small, testFormatInfo()));
  TEST_ASSERT_TRUE(small.data.empty());
}

TEST_CASE("One damaged superblock copy is survivable, two that disagree are not", "[sdlayout]")
{
  MemoryDevice card(kMinCardSectors);
  TEST_ASSERT_TRUE(Format(card, testFormatInfo()));

  card.Corrupt(kLogPartitionLba);  // Bad CRC: the backup is used
  TEST_ASSERT_EQUAL(CardClass::Ours, Classify(card).cardClass);

  // An interrupted format: a valid backup from a new format beside a valid primary from the old one.
  MemoryDevice card2(kMinCardSectors);
  TEST_ASSERT_TRUE(Format(card2, testFormatInfo()));
  Superblock sb;
  std::memcpy(&sb, card2.data[kDumpPartitionLba + 1].data(), sizeof(sb));
  sb.epoch = 0x12345678;
  sb.crc32 = SectorCrc(&sb);
  card2.WriteSector(kDumpPartitionLba + 1, reinterpret_cast<const uint8_t*>(&sb));
  TEST_ASSERT_EQUAL(CardClass::Foreign, Classify(card2).cardClass);
}

TEST_CASE("A superblock of an unknown version makes the card foreign", "[sdlayout]")
{
  MemoryDevice card(kMinCardSectors);
  TEST_ASSERT_TRUE(Format(card, testFormatInfo()));
  for (uint32_t lba : {kLogPartitionLba, kLogPartitionLba + 1}) {
    Superblock sb;
    std::memcpy(&sb, card.data[lba].data(), sizeof(sb));
    sb.version = 2;
    sb.crc32   = SectorCrc(&sb);
    card.WriteSector(lba, reinterpret_cast<const uint8_t*>(&sb));
  }
  TEST_ASSERT_EQUAL(CardClass::Foreign, Classify(card).cardClass);
}

TEST_CASE("A freshly formatted full-size ring is empty and found in a handful of reads", "[sdlayout]")
{
  MemoryDevice card(kMinCardSectors);
  TEST_ASSERT_TRUE(Format(card, testFormatInfo()));
  auto cls = Classify(card);

  card.reads    = 0;
  RingHead head = recover(card, cls.geometry);
  TEST_ASSERT_TRUE(head.empty);
  TEST_ASSERT_LESS_THAN(100, card.reads);
}

TEST_CASE("The head is found before and after the ring wraps", "[sdlayout]")
{
  Geometry g = smallGeometry(16);
  MemoryDevice card(kMinCardSectors);

  writeBlocks(card, g, 5);
  RingHead head = recover(card, g);
  TEST_ASSERT_FALSE(head.empty);
  TEST_ASSERT_EQUAL_UINT64(4, head.seq);

  writeBlocks(card, g, 15);  // seq 5..19: wraps to position 3
  head = recover(card, g);
  TEST_ASSERT_EQUAL_UINT64(19, head.seq);
  TEST_ASSERT_EQUAL_UINT64(19, readBlock(card, g, 3).seq);
  TEST_ASSERT_EQUAL_UINT64(4, readBlock(card, g, 4).seq);  // The previous lap is still there after the head

  writeBlocks(card, g, 12);                                // seq 20..31: exactly fills lap 1
  TEST_ASSERT_EQUAL_UINT64(31, recover(card, g).seq);
}

TEST_CASE("A block torn at the start of a new lap leaves the head at the end of the ring", "[sdlayout]")
{
  Geometry g = smallGeometry(16);
  MemoryDevice card(kMinCardSectors);
  writeBlocks(card, g, 17);  // seq 16 lands on position 0
  card.Corrupt(g.RingLba(0));

  RingHead head = recover(card, g);
  TEST_ASSERT_EQUAL_UINT64(15, head.seq);

  // The writer writes the torn position again, with the same seq.
  std::array<LogBlock, 2> buffer;
  LogRing ring(buffer);
  ring.Open(card, g, head, 1);
  TEST_ASSERT_EQUAL_UINT64(16, ring.NextSeq());
}

TEST_CASE("Blocks of another epoch or with a bad CRC never count", "[sdlayout]")
{
  Geometry old = smallGeometry(16, 0x11111111);
  MemoryDevice card(kMinCardSectors);
  writeBlocks(card, old, 10);  // An earlier format's content

  Geometry g = smallGeometry(16, 0x22222222);
  TEST_ASSERT_TRUE(recover(card, g).empty);
  writeBlocks(card, g, 3);
  TEST_ASSERT_EQUAL_UINT64(2, recover(card, g).seq);
}

TEST_CASE("Damage in the middle of the ring falls back to a full scan", "[sdlayout]")
{
  Geometry g = smallGeometry(256);
  MemoryDevice card(kMinCardSectors);
  writeBlocks(card, g, 200);
  for (uint32_t p = 40; p < 100; p++) {
    card.Corrupt(g.RingLba(p));  // Defeats the binary search and fails the probes
  }

  bool scanned  = false;
  RingHead head = recover(card, g, &scanned);
  TEST_ASSERT_TRUE(scanned);
  TEST_ASSERT_EQUAL_UINT64(199, head.seq);
}

TEST_CASE("LogRing packs whole records, carries boot and uptime, and splits writes at the ring end", "[sdlayout]")
{
  Geometry g = smallGeometry(8);
  MemoryDevice card(kMinCardSectors);
  writeBlocks(card, g, 6);  // Next write at position 6

  std::array<LogBlock, 8> buffer;
  LogRing ring(buffer);
  ring.Open(card, g, recover(card, g), 7);

  // ~100-byte records: four per block, so 16 records fill four blocks: positions 6, 7, 0, 1.
  std::string message(84, 'x');
  for (int i = 0; i < 16; i++) {
    TEST_ASSERT_TRUE(ring.AddLog(1, "tag", message, 1000 + i));
  }
  TEST_ASSERT_TRUE(ring.Flush());
  TEST_ASSERT_EQUAL_UINT64(10, ring.NextSeq());

  LogBlock first = readBlock(card, g, 6);
  TEST_ASSERT_EQUAL_UINT64(6, first.seq);
  TEST_ASSERT_EQUAL_UINT32(7, first.boot);
  TEST_ASSERT_EQUAL_UINT64(1000, first.uptimeMs);
  auto records = parseRecords(first);
  TEST_ASSERT_EQUAL(4, records.size());
  uint32_t delta;
  std::memcpy(&delta, records[3].data.data(), 4);
  TEST_ASSERT_EQUAL_UINT32(3, delta);
  TEST_ASSERT_EQUAL_STRING(message.c_str(), logMessage(records[3]).c_str());

  LogBlock wrapped = readBlock(card, g, 1);
  TEST_ASSERT_EQUAL_UINT64(9, wrapped.seq);
  TEST_ASSERT_EQUAL_UINT64(1012, wrapped.uptimeMs);
  TEST_ASSERT_EQUAL_UINT64(9, recover(card, g).seq);
}

TEST_CASE("LogRing reports a full buffer and never rewrites a written block", "[sdlayout]")
{
  Geometry g = smallGeometry(64);
  MemoryDevice card(kMinCardSectors);
  std::array<LogBlock, 2> buffer;
  LogRing ring(buffer);
  ring.Open(card, g, recover(card, g), 0);

  std::string message(300, 'm');
  TEST_ASSERT_TRUE(ring.AddLog(3, "a", message, 0));
  TEST_ASSERT_TRUE(ring.AddLog(3, "b", message, 0));   // Second block
  TEST_ASSERT_FALSE(ring.AddLog(3, "c", message, 0));  // No third
  TEST_ASSERT_TRUE(ring.Flush());
  TEST_ASSERT_TRUE(ring.AddLog(3, "c", message, 0));

  // A partial block is written as it is; the next flush starts a new block.
  TEST_ASSERT_TRUE(ring.Flush());
  TEST_ASSERT_TRUE(ring.AddLog(3, "d", "short", 0));
  TEST_ASSERT_TRUE(ring.Flush());
  TEST_ASSERT_EQUAL(1, parseRecords(readBlock(card, g, 2)).size());
  TEST_ASSERT_EQUAL(1, parseRecords(readBlock(card, g, 3)).size());
  TEST_ASSERT_EQUAL_UINT64(4, ring.NextSeq());
}

TEST_CASE("LogRing cuts an oversized message on a UTF-8 boundary and flags it", "[sdlayout]")
{
  Geometry g = smallGeometry(16);
  MemoryDevice card(kMinCardSectors);
  std::array<LogBlock, 4> buffer;
  LogRing ring(buffer);
  ring.Open(card, g, recover(card, g), 0);

  std::string message;
  while (message.size() < 600) {
    message += "\xC3\xA6";               // 'æ', two bytes
  }
  message.insert(message.begin(), 'x');  // Odd offset, so the cut would land mid-character
  TEST_ASSERT_TRUE(ring.AddLog(2, "tag", message, 0));
  TEST_ASSERT_TRUE(ring.Flush());

  auto records = parseRecords(readBlock(card, g, 0));
  TEST_ASSERT_EQUAL(1, records.size());
  TEST_ASSERT_EQUAL_HEX8(kRecordFlagTruncated, records[0].flags);
  std::string cut = logMessage(records[0]);
  TEST_ASSERT_LESS_THAN(message.size(), cut.size());
  TEST_ASSERT_EQUAL(1, cut.size() % 2);  // 'x' plus whole two-byte characters
  TEST_ASSERT_EQUAL_MEMORY(message.data(), cut.data(), cut.size());
}

TEST_CASE("LogRing keeps its blocks for a retry when a flush fails", "[sdlayout]")
{
  Geometry g = smallGeometry(16);
  MemoryDevice card(kMinCardSectors);
  std::array<LogBlock, 4> buffer;
  LogRing ring(buffer);
  ring.Open(card, g, recover(card, g), 0);

  TEST_ASSERT_TRUE(ring.AddLog(1, "tag", "keep me", 5));
  card.failWrites = true;
  TEST_ASSERT_FALSE(ring.Flush());
  card.failWrites = false;
  TEST_ASSERT_TRUE(ring.Flush());
  TEST_ASSERT_EQUAL_STRING("keep me", logMessage(parseRecords(readBlock(card, g, 0))[0]).c_str());
  TEST_ASSERT_EQUAL_UINT64(1, ring.NextSeq());
}

namespace {
  std::vector<uint8_t> testDump(std::size_t size)
  {
    std::vector<uint8_t> dump(size);
    for (std::size_t i = 0; i < size; i++) {
      dump[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    return dump;
  }

  std::optional<SlotHeader> storeDump(DumpStore& store, const std::vector<uint8_t>& dump, uint32_t boot)
  {
    SlotHeader header {};
    header.storedBoot = boot;
    header.originBoot = boot - 1;
    header.objectType = kObjectCoredump;
    header.excPc      = 0x400D1234;
    std::strcpy(header.excTask, "loopTask");
    std::strcpy(header.storedFwVersion, "1.6.0-test");
    std::strcpy(header.board, "Host-Test-Board");
    for (int i = 0; i < 32; i++) {
      header.appElfSha256[i] = static_cast<uint8_t>(0xA0 + i);
    }

    std::vector<uint8_t> scratch(4096);
    auto reader = [&](uint32_t offset, uint8_t* out, std::size_t len) {
      std::memcpy(out, dump.data() + offset, len);
      return true;
    };
    return store.Store(header, static_cast<uint32_t>(dump.size()), reader, scratch);
  }
}  // namespace

TEST_CASE("DumpStore commits a dump into slot seq % 255 and finds it again", "[sdlayout]")
{
  MemoryDevice card(kMinCardSectors);
  TEST_ASSERT_TRUE(Format(card, testFormatInfo()));
  auto g = Classify(card).geometry;

  DumpStore store;
  TEST_ASSERT_TRUE(store.Open(card, g));
  TEST_ASSERT_FALSE(store.Head().has_value());

  auto dump   = testDump(10'000);
  auto stored = storeDump(store, dump, 3);
  TEST_ASSERT_TRUE(stored.has_value());
  TEST_ASSERT_EQUAL_UINT64(0, stored->seq);
  TEST_ASSERT_EQUAL_UINT32(10'000, stored->size);

  uint32_t lba = Geometry::SlotLba(0);
  TEST_ASSERT_EQUAL_UINT32(kDumpPartitionLba + 2048, lba);
  TEST_ASSERT_EQUAL_MEMORY(dump.data(), card.data[lba + 1].data(), kSectorSize);

  auto second = storeDump(store, testDump(600), 4);
  TEST_ASSERT_EQUAL_UINT64(1, second->seq);

  DumpStore reopened;
  TEST_ASSERT_TRUE(reopened.Open(card, g));
  TEST_ASSERT_EQUAL_UINT64(1, reopened.Head()->seq);
  TEST_ASSERT_EQUAL_UINT64(2, reopened.NextSeq());

  // A slot whose header is damaged doesn't count; the head goes back to the one before.
  card.Corrupt(Geometry::SlotLba(1));
  TEST_ASSERT_TRUE(reopened.Open(card, g));
  TEST_ASSERT_EQUAL_UINT64(0, reopened.Head()->seq);
}

TEST_CASE("DumpStore refuses an object larger than a slot", "[sdlayout]")
{
  MemoryDevice card(kMinCardSectors);
  TEST_ASSERT_TRUE(Format(card, testFormatInfo()));
  DumpStore store;
  store.Open(card, Classify(card).geometry);
  TEST_ASSERT_FALSE(storeDump(store, testDump(kSlotDataBytes + 1), 1).has_value());
}

// ---------------------------------------------------------------------------------------------------------------------
// Golden card image for tools/sdlog (tools/sdlog/testdata/golden). Written only when OPENSHOCK_SDLOG_GOLDEN_DIR is set:
//
//   OPENSHOCK_SDLOG_GOLDEN_DIR=$PWD/tools/sdlog/testdata/golden ./build/diagnostics_host_test.elf
//
// The image is the sdlog "image directory" format: card.json plus extents/<lba>.bin runs of sectors; sectors that are
// all zero are left out. tools/sdlog/test_sdlog.py checks what sdlog makes of it; change both together.

namespace {
  void writeImageDir(const MemoryDevice& card, const std::string& dir)
  {
    namespace fs = std::filesystem;
    fs::remove_all(dir);
    fs::create_directories(fs::path(dir) / "extents");

    std::ofstream(fs::path(dir) / "card.json") << "{\"card_sectors\": " << card.SectorCount() << ", \"sector_size\": 512}\n";

    auto isZero = [](const Sector& s) {
      return std::all_of(s.begin(), s.end(), [](uint8_t b) { return b == 0; });
    };
    auto it = card.data.begin();
    while (it != card.data.end()) {
      if (isZero(it->second)) {
        ++it;
        continue;
      }
      uint32_t start = it->first;
      std::ofstream out(fs::path(dir) / "extents" / (std::to_string(start) + ".bin"), std::ios::binary);
      uint32_t next = start;
      while (it != card.data.end() && it->first == next && !isZero(it->second)) {
        out.write(reinterpret_cast<const char*>(it->second.data()), kSectorSize);
        ++it;
        ++next;
      }
    }
  }

  void addBoot(LogRing& ring, uint8_t resetReason, uint64_t uptimeMs)
  {
    std::vector<uint8_t> data = {resetReason, 0, 0, 0, 0x02, 0x00, 0x5E, 0x10, 0x20, 0x30};
    for (std::string_view s : {"1.6.0-test", "Host-Test-Board", "ESP32-S3", "v6.1"}) {
      data.push_back(static_cast<uint8_t>(s.size()));
      data.insert(data.end(), s.begin(), s.end());
    }
    TEST_ASSERT_TRUE(ring.Add(RecordType::Boot, data, uptimeMs));
  }
}  // namespace

TEST_CASE("Golden card image", "[sdlayout][golden]")
{
  const char* dir = std::getenv("OPENSHOCK_SDLOG_GOLDEN_DIR");

  MemoryDevice card(kMinCardSectors);
  TEST_ASSERT_TRUE(Format(card, testFormatInfo()));
  Geometry g = Classify(card).geometry;
  std::array<LogBlock, 8> buffer;

  // Boot 0: power-on, a time sync, a few lines, a long line that gets cut, drops, a clean eject.
  {
    LogRing ring(buffer);
    ring.Open(card, g, recover(card, g), 0);
    const uint8_t mount[4] = {1, 0, 0, 0};
    TEST_ASSERT_TRUE(ring.Add(RecordType::Mount, mount, 100));
    addBoot(ring, 1 /* ESP_RST_POWERON */, 100);
    TEST_ASSERT_TRUE(ring.AddLog(3, "main", "Board: Host-Test-Board (ESP32-S3)", 120));
    TEST_ASSERT_TRUE(ring.AddTimeSync(1'790'000'000'000'000LL, TimeSource::Backend, 5000));
    TEST_ASSERT_TRUE(ring.AddLog(2, "WiFiManager", "Connected to \"home\"", 5100));
    TEST_ASSERT_TRUE(ring.AddLog(1, "RFTransmitter", "Failed to send command", 6000));
    TEST_ASSERT_TRUE(ring.AddLog(4, "esp-tls", std::string(700, 'z'), 6500));
    const uint8_t dropped[8] = {3, 0, 0, 0, 150, 0, 0, 0};
    TEST_ASSERT_TRUE(ring.Add(RecordType::Dropped, dropped, 7000));
    TEST_ASSERT_TRUE(ring.Add(RecordType::Unmount, {}, 8000));
    TEST_ASSERT_TRUE(ring.Flush());
  }

  // Boot 1: after a panic; no time sync, the coredump is stored and logged.
  {
    LogRing ring(buffer);
    RingHead head = recover(card, g);
    ring.Open(card, g, head, head.boot + 1);
    const uint8_t mount[4] = {1, 0, 0, 0};
    TEST_ASSERT_TRUE(ring.Add(RecordType::Mount, mount, 90));
    addBoot(ring, 4 /* ESP_RST_PANIC */, 90);

    DumpStore store;
    TEST_ASSERT_TRUE(store.Open(card, g));
    auto stored = storeDump(store, testDump(3000), 1);
    TEST_ASSERT_TRUE(stored.has_value());
    uint8_t ds[24]  = {};
    uint32_t slot   = 0;
    uint16_t type   = kObjectCoredump;
    uint32_t size   = 3000;
    uint32_t origin = 0;
    std::memcpy(ds, &stored->seq, 8);
    std::memcpy(ds + 8, &slot, 4);
    std::memcpy(ds + 12, &type, 2);
    std::memcpy(ds + 16, &size, 4);
    std::memcpy(ds + 20, &origin, 4);
    TEST_ASSERT_TRUE(ring.Add(RecordType::DumpStored, ds, 400));
    TEST_ASSERT_TRUE(ring.AddLog(1, "main", "PANIC: something broke", 1500));
    TEST_ASSERT_TRUE(ring.Flush());
  }

  TEST_ASSERT_EQUAL_UINT64(3, recover(card, g).seq + 1);  // Boot 0 needed two blocks (the cut line), boot 1 one

  if (dir != nullptr) {
    writeImageDir(card, dir);
  }
}
