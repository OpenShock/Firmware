#pragma once

#include "diagnostics/SdCard.h"  // TimeSource
#include "sdcard/BlockDevice.h"

#include "util/FunctionRef.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// The on-card layout of a diagnostics SD card, as specified in docs/SDCARD.md ("OpenShock SD Card Log Storage: Layout
// and Format"). No filesystem: an MBR written from scratch with two partitions in formats of their own.
//
//   LBA 0          MBR, no boot code
//   LBA 8,192      dump partition (type 0xFA, 256 MiB): superblock x2, reserved, 255 slots of 1 MiB
//   LBA 532,480    log partition (type 0x7F, rest of the card): superblock x2, reserved, ring of 512-byte blocks
//
// Superblocks are written once, at format. All mutable state lives in the ring blocks and slot headers, each carrying
// the format's random epoch, its own sequence number and a CRC. tools/sdlog/sdlog.py reads the same layout on a PC;
// change both together. All integers are little-endian, as both the chips and PCs store them.
namespace OpenShock::Diagnostics::SdCard {
  static_assert(std::endian::native == std::endian::little);

  inline constexpr uint32_t kMinCardSectors = 1u << 21;  // 1 GiB
  inline constexpr uint32_t kAlignSectors   = 8192;      // 4 MiB, the SD allocation unit up to SDXC

  inline constexpr uint8_t kDumpPartitionType     = 0xFA;
  inline constexpr uint32_t kDumpPartitionLba     = 8192;
  inline constexpr uint32_t kDumpPartitionSectors = 524'288;  // 256 MiB
  inline constexpr uint32_t kDumpDataStart        = 2048;
  inline constexpr uint32_t kSlotSectors          = 2048;     // 1 MiB: a header sector and 2,047 data sectors
  inline constexpr uint32_t kSlotCount            = 255;
  inline constexpr uint32_t kSlotDataBytes        = (kSlotSectors - 1) * kSectorSize;
  static_assert(kDumpDataStart + kSlotCount * kSlotSectors == kDumpPartitionSectors);

  inline constexpr uint8_t kLogPartitionType = 0x7F;
  inline constexpr uint32_t kLogPartitionLba = kDumpPartitionLba + kDumpPartitionSectors;  // 532,480
  inline constexpr uint32_t kRingDataStart   = 8;

  inline constexpr uint16_t kLayoutVersion = 1;

  inline constexpr char kLogSuperblockMagic[8]  = {'O', 'S', 'L', 'O', 'G', 'S', 'B', '\0'};
  inline constexpr char kDumpSuperblockMagic[8] = {'O', 'S', 'D', 'M', 'P', 'S', 'B', '\0'};
  inline constexpr uint32_t kBlockMagic         = 0x4B4C424F;  // "OBLK"
  inline constexpr uint32_t kSlotMagic          = 0x504D444F;  // "ODMP"

  struct MbrPartitionEntry {
    uint8_t status;       // 0x00: not bootable
    uint8_t chsFirst[3];  // 0xFE 0xFF 0xFF: use the LBA fields
    uint8_t type;
    uint8_t chsLast[3];
    uint32_t lbaStart;
    uint32_t sectors;
  };
  static_assert(sizeof(MbrPartitionEntry) == 16);

  inline constexpr std::size_t kMbrSignatureOffset = 0x1B8;
  inline constexpr std::size_t kMbrEntriesOffset   = 0x1BE;
  inline constexpr std::size_t kMbrBootSigOffset   = 0x1FE;

  struct Superblock {
    char magic[8];
    uint16_t version;
    uint16_t flags;
    uint32_t epoch;        // Random, never 0 or 0xFFFFFFFF; the same in both partitions
    uint32_t unitSectors;  // 1 (a block) / 2,048 (a slot)
    uint32_t dataStart;    // 8 / 2,048
    uint32_t unitCount;    // ring_blocks / 255
    uint32_t partitionLba;
    uint32_t partitionSectors;
    uint32_t cardSectors;
    uint64_t formatUnixS;  // 0 if unknown
    uint8_t mac[6];
    uint16_t reserved0;
    char fwVersion[32];
    char board[32];
    uint8_t reserved1[388];
    uint32_t crc32;  // Over 0x000 to 0x1FB
  };
  static_assert(sizeof(Superblock) == kSectorSize);
  static_assert(offsetof(Superblock, formatUnixS) == 0x28 && offsetof(Superblock, fwVersion) == 0x38 && offsetof(Superblock, board) == 0x58 && offsetof(Superblock, crc32) == 0x1FC);

  inline constexpr std::size_t kBlockPayloadSize = 476;

  struct LogBlock {
    uint32_t magic;       // kBlockMagic
    uint32_t epoch;
    uint64_t seq;         // Global within the epoch, from 0; lives at ring position seq % ring_blocks
    uint32_t boot;        // Boot counter within the epoch, from 0
    uint16_t payloadLen;  // 0 to kBlockPayloadSize
    uint16_t flags;       // 0
    uint64_t uptimeMs;    // Uptime at the block's first record
    uint8_t payload[kBlockPayloadSize];
    uint32_t crc32;       // Over 0x000 to 0x1FB
  };
  static_assert(sizeof(LogBlock) == kSectorSize);
  static_assert(offsetof(LogBlock, uptimeMs) == 0x18 && offsetof(LogBlock, payload) == 0x20 && offsetof(LogBlock, crc32) == 0x1FC);

  struct SlotHeader {
    uint32_t magic;       // kSlotMagic
    uint32_t epoch;
    uint64_t seq;         // Object sequence within the epoch, from 0; lives in slot seq % 255
    uint32_t storedBoot;  // Log boot counter of the boot that stored it
    uint32_t originBoot;  // ... of the boot that crashed (storedBoot - 1 for a coredump)
    uint16_t objectType;  // kObjectCoredump
    uint16_t flags;       // 0
    uint32_t size;        // Data bytes
    uint32_t dataCrc32;
    uint8_t resetReason;  // esp_reset_reason() at the storing boot
    uint8_t reserved0[3];
    uint64_t storedUptimeMs;
    uint8_t appElfSha256[32];  // The crashed app's ELF SHA-256 (as much of it as the dump records; the rest zero)
    char storedFwVersion[32];
    char board[32];
    uint32_t excPc;
    char excTask[16];
    uint8_t reserved1[344];
    uint32_t crc32;  // Over 0x000 to 0x1FB
  };
  static_assert(sizeof(SlotHeader) == kSectorSize);
  static_assert(offsetof(SlotHeader, storedUptimeMs) == 0x28 && offsetof(SlotHeader, appElfSha256) == 0x30 && offsetof(SlotHeader, excPc) == 0x90 && offsetof(SlotHeader, excTask) == 0x94 && offsetof(SlotHeader, crc32) == 0x1FC);

  inline constexpr uint16_t kObjectCoredump = 1;

  // Records, packed back to back in a block's payload. A record never spans two blocks.
  enum class RecordType : uint8_t {
    Invalid    = 0x00,
    Log        = 0x01,  // delta_ms u32, level u8 (1 error .. 5 verbose), tag_len u8, tag, message
    Boot       = 0x02,  // reset_reason u8, app_slot u8, 2 reserved, mac[6], then fw_version, board, variant, idf_version (u8 length each)
    TimeSync   = 0x03,  // delta_ms u32, unix_us i64, source u8, 3 reserved
    DumpStored = 0x04,  // object_seq u64, slot u32, object_type u16, 2 reserved, size u32, origin_boot u32
    Dropped    = 0x05,  // records u32, bytes u32 dropped since the previous Dropped
    Mount      = 0x06,  // reason u8 (MountReason), 3 reserved
    Unmount    = 0x07,  // none
    End        = 0xFF,
  };
  inline constexpr std::size_t kRecordHeaderSize = 4;  // type u8, flags u8, len u16
  inline constexpr uint8_t kRecordFlagTruncated  = 1u << 0;

  enum class MountReason : uint8_t {
    Boot       = 1,
    Reinserted = 2,
  };

  /// @brief CRC-32/ISO-HDLC (zlib's crc32) over the first 0x1FC bytes of a sector-sized struct.
  uint32_t SectorCrc(const void* sector);

  struct Geometry {
    uint32_t cardSectors;
    uint32_t epoch;
    uint32_t logSectors;
    uint32_t ringBlocks;

    uint32_t RingLba(uint32_t position) const { return kLogPartitionLba + kRingDataStart + position; }
    static uint32_t SlotLba(uint32_t slot) { return kDumpPartitionLba + kDumpDataStart + slot * kSlotSectors; }
  };

  /// @brief The layout of a card of `cardSectors`; nullopt if it is smaller than 1 GiB.
  std::optional<Geometry> PlanGeometry(uint32_t cardSectors, uint32_t epoch);

  enum class CardClass : uint8_t {
    Ours,     // Our MBR and valid, agreeing superblocks: the only class ever written to
    Blank,    // Sector 0 all 0x00 or all 0xFF
    Foreign,  // Anything else
    IoError,  // Sector 0 or a superblock could not be read
  };

  struct Classification {
    CardClass cardClass;
    Geometry geometry;  // Valid for Ours
  };

  Classification Classify(BlockDevice& device);

  struct FormatInfo {
    uint32_t epoch;  // Never 0 or 0xFFFFFFFF
    uint32_t diskSignature;
    uint64_t formatUnixS;
    uint8_t mac[6];
    std::string_view fwVersion;
    std::string_view board;
  };

  /// @brief Formats the card: zeroes the reserved sectors, writes the superblocks (backup first) and the MBR last,
  ///        reading every sector back. Destroys what the card held. The caller re-classifies afterwards.
  bool Format(BlockDevice& device, const FormatInfo& info);

  struct RingHead {
    bool empty;     // No valid block: the next write is seq 0, boot 0
    uint64_t seq;   // Of the head block
    uint32_t boot;  // Of the head block
    bool fullScan;  // Verification failed and the head came from a linear scan
  };

  /// @brief Finds the newest block: a binary search over lap numbers, verified with 64 probes, falling back to a full
  ///        linear scan when blocks in the middle of the ring are damaged. nullopt on a read error.
  /// @param onFullScan Called once before a full scan starts (it can take minutes on a large card).
  std::optional<RingHead> RecoverHead(BlockDevice& device, const Geometry& geometry, Util::FunctionRef<void()> onFullScan);

  /// @brief Packs records into blocks and appends them to the ring. Holds up to `buffer.size()` blocks; Flush() writes
  ///        them in one multi-sector write (two where the ring wraps) and never touches a block once written.
  class LogRing {
  public:
    /// @param buffer Block buffer (DMA-capable on the device), owned by the caller and kept for this object's life.
    explicit LogRing(std::span<LogBlock> buffer) noexcept;

    /// @brief Continues after `head` (or at seq 0) with boot counter `boot`.
    void Open(BlockDevice& device, const Geometry& geometry, const RingHead& head, uint32_t boot);
    void Close();
    bool IsOpen() const { return m_device != nullptr; }

    /// @brief Adds a record. Returns false when the buffer is full: Flush() and add it again.
    bool Add(RecordType type, std::span<const uint8_t> data, uint64_t uptimeMs, uint8_t flags = 0);

    /// @brief Adds a Log record, cut to fit when it is too long for a block (flag kRecordFlagTruncated).
    ///        Returns false when the buffer is full.
    bool AddLog(uint8_t level, std::string_view tag, std::string_view message, uint64_t uptimeMs);

    /// @brief Adds a TimeSync record. Returns false when the buffer is full.
    bool AddTimeSync(int64_t unixUs, TimeSource source, uint64_t uptimeMs);

    bool HasPending() const { return m_sealed != 0 || m_fill != 0; }
    /// @brief Uptime of the oldest record not yet written.
    uint64_t OldestPendingUptimeMs() const { return m_blocks[0].uptimeMs; }

    /// @brief Finishes the current block and writes every finished block. On failure nothing is lost from the buffer;
    ///        the same blocks are written again by the next Flush().
    bool Flush();

    uint64_t NextSeq() const { return m_nextSeq; }
    uint32_t Boot() const { return m_boot; }
    uint32_t Epoch() const { return m_geometry.epoch; }

  private:
    // Room left in the current block, which is started if there is none yet. 0 when the buffer is full.
    std::size_t room(uint64_t uptimeMs, bool newBlock);
    uint8_t* reserve(RecordType type, uint8_t flags, std::size_t len);
    uint32_t deltaMs(uint64_t uptimeMs) const;

    std::span<LogBlock> m_blocks;
    BlockDevice* m_device = nullptr;
    Geometry m_geometry {};
    uint64_t m_nextSeq = 0;
    uint32_t m_boot    = 0;
    uint32_t m_sealed  = 0;  // Finished blocks at the front of the buffer
    std::size_t m_fill = 0;  // Payload bytes in the current block, m_blocks[m_sealed]; 0 when it is not started
  };

  /// @brief Produces `len` bytes of the object starting at `offset`.
  using ObjectReader = Util::FunctionRef<bool(uint32_t offset, uint8_t* out, std::size_t len)>;

  /// @brief The dump partition's slots.
  class DumpStore {
  public:
    /// @brief Reads all 255 slot headers and finds the newest. False on a read error.
    bool Open(BlockDevice& device, const Geometry& geometry);
    void Close() { m_device = nullptr; }

    const std::optional<SlotHeader>& Head() const { return m_head; }
    uint64_t NextSeq() const { return m_head ? m_head->seq + 1 : 0; }

    /// @brief Stores an object of `size` bytes in slot NextSeq() % 255: invalidates the slot's header, copies the data,
    ///        commits the header, then reads both back and checks their CRCs. `header` supplies the descriptive fields;
    ///        magic, epoch, seq, size and the CRCs are filled in. `scratch` must hold at least 8 sectors.
    /// @return The stored header, nullopt on any failure (the head then stays where it was).
    std::optional<SlotHeader> Store(SlotHeader header, uint32_t size, ObjectReader reader, std::span<uint8_t> scratch);

    /// @brief CRC-32 of `size` bytes from `reader`, read through `scratch`.
    static std::optional<uint32_t> Checksum(uint32_t size, ObjectReader reader, std::span<uint8_t> scratch);

  private:
    BlockDevice* m_device = nullptr;
    Geometry m_geometry {};
    std::optional<SlotHeader> m_head;
  };
}  // namespace OpenShock::Diagnostics::SdCard
