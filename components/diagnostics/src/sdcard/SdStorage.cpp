#include <freertos/FreeRTOS.h>

#include "diagnostics/SdCard.h"

const char* const TAG = "SdStorage";

#include "BoardDescriptor.h"
#include "Coredump.h"
#include "Internal.h"
#include "Logging.h"
#include "OpenShock.h"
#include "sdcard/SdHostCard.h"
#include "sdcard/SdLayout.h"
#include "Temporal.h"
#include "util/HexUtils.h"
#include "util/TaskUtils.h"

#include <driver/gpio.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_system.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>
#include <string_view>

using namespace OpenShock;
using namespace OpenShock::Diagnostics;
using namespace OpenShock::Diagnostics::SdCard;

static constexpr std::size_t kBufferBlocks      = 8;     // 4 KiB of blocks per flush
static constexpr int64_t kFlushIntervalMs       = 2000;  // Oldest unflushed record waits at most this long
static constexpr TickType_t kPollTicks          = pdMS_TO_TICKS(50);
static constexpr int64_t kDebounceMs            = 50;
static constexpr int64_t kTimeSyncIntervalMs    = 6LL * 60 * 60 * 1000;
static constexpr TickType_t kShutdownFlushTicks = pdMS_TO_TICKS(200);
static constexpr std::size_t kCopyScratchSize   = 4096;
static constexpr std::size_t kMaxQueuedMessage  = kBlockPayloadSize;

// A queued record: this header, then the tag, then the message (Log) or a QueuedTimeSync (TimeSync).
enum class QueuedKind : uint8_t {
  Log,
  TimeSync,
};
struct QueuedHeader {
  uint64_t uptimeMs;
  QueuedKind kind;
  uint8_t level;
  uint8_t tagLen;
  uint8_t reserved;
};
struct QueuedTimeSync {
  int64_t unixUs;
  uint8_t source;
};

enum Request : uint32_t {
  RequestNone   = 0,
  RequestFormat = 1,
  RequestEject  = 2,
  RequestFlush  = 3,
};

static RingbufHandle_t s_queue = nullptr;
static TaskHandle_t s_task     = nullptr;
static std::atomic<State> s_state {State::Disabled};
static std::atomic<uint32_t> s_droppedRecords {0};  // Since the last Dropped record
static std::atomic<uint32_t> s_droppedBytes {0};
static std::atomic<uint32_t> s_droppedRecordsTotal {0};
static std::atomic<uint32_t> s_droppedBytesTotal {0};

static SemaphoreHandle_t s_requestMutex = nullptr;  // One request at a time
static SemaphoreHandle_t s_requestDone  = nullptr;
static std::atomic<uint32_t> s_request {RequestNone};
static std::atomic<bool> s_requestResult {false};

// Owned by the storage task (and by SdCardInit before the task exists).
static SdHostCard s_card;
static LogBlock s_blocks[kBufferBlocks];
static LogRing s_ring(s_blocks);
static DumpStore s_dumps;
static Geometry s_geometry {};
static bool s_mountedThisBoot      = false;  // A BOOT record has gone to some card this boot
static uint32_t s_lastEpoch        = 0;      // Card mounted last, and the boot counter it used
static uint32_t s_lastBoot         = 0;
static bool s_haveTime             = false;  // Last TIME_SYNC, to repeat it every 6 hours and after a mount
static int64_t s_syncUnixUs        = 0;
static int64_t s_syncUptimeMs      = 0;
static int64_t s_lastSyncWrittenMs = 0;
static bool s_cardPresent          = false;  // Debounced card detect

static bool readCardDetect()
{
  int level = gpio_get_level(static_cast<gpio_num_t>(CONFIG_OPENSHOCK_DIAG_SDCARD_CD_GPIO));
#if CONFIG_OPENSHOCK_DIAG_SDCARD_CD_ACTIVE_LOW
  return level == 0;
#else
  return level != 0;
#endif
}

bool Detail::SdCardConfigured()
{
  return s_state.load() != State::Disabled;
}

const char* SdCard::StateName(State state)
{
  switch (state) {
    case State::Disabled:
      return "disabled";
    case State::NoCard:
      return "no card";
    case State::Unusable:
      return "unusable";
    case State::NeedsFormat:
      return "needs formatting";
    case State::Recovering:
      return "recovering";
    case State::Mounted:
      return "mounted";
    case State::Ejected:
      return "safe to remove";
    case State::Failed:
      return "failed";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------------------------------------------------
// Producers: never block, drop and count when the queue is full.

static void countDrop(std::size_t bytes)
{
  s_droppedRecords.fetch_add(1, std::memory_order_relaxed);
  s_droppedBytes.fetch_add(static_cast<uint32_t>(bytes), std::memory_order_relaxed);
  s_droppedRecordsTotal.fetch_add(1, std::memory_order_relaxed);
  s_droppedBytesTotal.fetch_add(static_cast<uint32_t>(bytes), std::memory_order_relaxed);
}

void Detail::SdCardEnqueueLog(char level, const char* tag, std::size_t tagLen, const char* message, std::size_t len)
{
  if (s_queue == nullptr) {
    return;
  }

  tagLen         = std::min<std::size_t>(tagLen, 32);
  len            = std::min(len, kMaxQueuedMessage);
  std::size_t sz = sizeof(QueuedHeader) + tagLen + len;

  // Acquire/complete writes straight into the queue, so the logging task needs no copy on its stack.
  void* slot = nullptr;
  if (xRingbufferSendAcquire(s_queue, &slot, sz, 0) != pdTRUE || slot == nullptr) {
    countDrop(sz);
    return;
  }

  QueuedHeader header {
    .uptimeMs = static_cast<uint64_t>(OpenShock::millis()),
    .kind     = QueuedKind::Log,
    .level    = Detail::CardLevel(level),
    .tagLen   = static_cast<uint8_t>(tagLen),
    .reserved = 0,
  };
  auto* bytes = static_cast<uint8_t*>(slot);
  std::memcpy(bytes, &header, sizeof(header));
  std::memcpy(bytes + sizeof(header), tag, tagLen);
  std::memcpy(bytes + sizeof(header) + tagLen, message, len);
  xRingbufferSendComplete(s_queue, slot);
}

void SdCard::RecordTimeSync(int64_t unixUs, TimeSource source)
{
  if (s_queue == nullptr) {
    return;
  }

  uint8_t item[sizeof(QueuedHeader) + sizeof(QueuedTimeSync)] = {};
  QueuedHeader header {.uptimeMs = static_cast<uint64_t>(OpenShock::millis()), .kind = QueuedKind::TimeSync, .level = 0, .tagLen = 0, .reserved = 0};
  QueuedTimeSync sync {.unixUs = unixUs, .source = static_cast<uint8_t>(source)};
  std::memcpy(item, &header, sizeof(header));
  std::memcpy(item + sizeof(header), &sync, sizeof(sync));
  if (xRingbufferSend(s_queue, item, sizeof(item), 0) != pdTRUE) {
    countDrop(sizeof(item));
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Writer (storage task only)

static void shutDownCard(State state)
{
  s_ring.Close();
  s_dumps.Close();
  s_card.Deinit();
  s_state.store(state);
}

// Writes the ring's buffered blocks. On failure: reinitialise the card once and retry; a second failure marks the card
// failed and stops the writer until it is reinserted.
static bool flushRing()
{
  if (!s_ring.IsOpen() || s_ring.Flush()) {
    return true;
  }

  OS_LOGW(TAG, "SD card write failed, reinitialising the card and retrying");
  if (s_card.Init() && s_ring.Flush()) {
    return true;
  }

  OS_LOGE(TAG, "SD card write failed again, giving up on the card until it is reinserted");
  shutDownCard(State::Failed);
  return false;
}

// Adds a record, flushing first when the block buffer is full.
template<typename AddFn>
static bool addRecord(AddFn add)
{
  if (add()) {
    return true;
  }
  return flushRing() && add();
}

static void addDroppedIfAny(uint64_t uptimeMs)
{
  uint32_t records = s_droppedRecords.exchange(0, std::memory_order_relaxed);
  uint32_t bytes   = s_droppedBytes.exchange(0, std::memory_order_relaxed);
  if (records == 0) {
    return;
  }

  uint8_t data[8];
  std::memcpy(data, &records, 4);
  std::memcpy(data + 4, &bytes, 4);
  addRecord([&] { return s_ring.Add(RecordType::Dropped, data, uptimeMs); });
}

static void addTimeSync(int64_t unixUs, TimeSource source, uint64_t uptimeMs)
{
  if (addRecord([&] { return s_ring.AddTimeSync(unixUs, source, uptimeMs); })) {
    s_lastSyncWrittenMs = static_cast<int64_t>(uptimeMs);
  }
}

static void addBootRecord(uint64_t uptimeMs)
{
  uint8_t data[kBlockPayloadSize];
  std::size_t len = 0;

  const esp_partition_t* running = esp_ota_get_running_partition();
  data[len++]                    = static_cast<uint8_t>(esp_reset_reason());
  data[len++]                    = running != nullptr && running->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN ? static_cast<uint8_t>(running->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN) : 0xFF;
  data[len++]                    = 0;
  data[len++]                    = 0;
  esp_efuse_mac_get_default(data + len);
  len += 6;

  auto putString = [&](std::string_view s) {
    s           = s.substr(0, std::min<std::size_t>(s.size(), 64));
    data[len++] = static_cast<uint8_t>(s.size());
    std::memcpy(data + len, s.data(), s.size());
    len += s.size();
  };
  putString(Constants::FW_VERSION);
  putString(Board::Name());
  putString(OPENSHOCK_FW_CHIP);  // The build variant id isn't known to the firmware; the chip names the compile
  putString(esp_get_idf_version());

  addRecord([&] { return s_ring.Add(RecordType::Boot, std::span<const uint8_t>(data, len), uptimeMs); });
}

static void addDumpStored(const SlotHeader& header, uint64_t uptimeMs)
{
  uint8_t data[24] = {};
  uint32_t slot    = static_cast<uint32_t>(header.seq % kSlotCount);
  std::memcpy(data, &header.seq, 8);
  std::memcpy(data + 8, &slot, 4);
  std::memcpy(data + 12, &header.objectType, 2);
  std::memcpy(data + 16, &header.size, 4);
  std::memcpy(data + 20, &header.originBoot, 4);
  addRecord([&] { return s_ring.Add(RecordType::DumpStored, data, uptimeMs); });
}

// Steps 1-8 of "Storing a coredump" in docs/SDCARD.md.
static void copyCoredump()
{
  Detail::LockCoredump();

  auto image = Coredump::Find();
  if (!image) {
    Detail::UnlockCoredump();
    return;
  }
  if (image->size > kSlotDataBytes) {
    Detail::UnlockCoredump();
    OS_LOGE(TAG, "Crash dump of %zu bytes does not fit a slot", image->size);
    return;
  }

  SlotHeader header {};
  header.storedBoot     = s_ring.Boot();
  header.originBoot     = s_ring.Boot() > 0 ? s_ring.Boot() - 1 : 0;
  header.objectType     = kObjectCoredump;
  header.resetReason    = static_cast<uint8_t>(esp_reset_reason());
  header.storedUptimeMs = static_cast<uint64_t>(OpenShock::millis());
  std::string_view fw   = Constants::FW_VERSION;
  std::memcpy(header.storedFwVersion, fw.data(), std::min(fw.size(), sizeof(header.storedFwVersion) - 1));
  std::memcpy(header.board, Board::Name().data(), std::min(Board::Name().size(), sizeof(header.board) - 1));
  if (auto summary = Coredump::GetSummary()) {
    for (std::size_t i = 0; i + 1 < summary->elfSha256.size() && i / 2 < sizeof(header.appElfSha256); i += 2) {
      auto byte = HexUtils::TryParseHexPair(summary->elfSha256[i], summary->elfSha256[i + 1]);
      if (!byte) {
        break;
      }
      header.appElfSha256[i / 2] = *byte;
    }
    header.excPc = summary->excPc;
    std::memcpy(header.excTask, summary->excTask, sizeof(header.excTask));
  }

  auto reader = [&image](uint32_t offset, uint8_t* out, std::size_t len) {
    return Coredump::Read(*image, offset, out, len);
  };

  uint8_t* scratch = static_cast<uint8_t*>(heap_caps_malloc(kCopyScratchSize, MALLOC_CAP_INTERNAL));
  if (scratch == nullptr) {
    Detail::UnlockCoredump();
    OS_LOGE(TAG, "Out of memory copying the crash dump");
    return;
  }

  std::optional<SlotHeader> stored;
  const auto& head = s_dumps.Head();
  auto crc         = DumpStore::Checksum(static_cast<uint32_t>(image->size), reader, std::span<uint8_t>(scratch, kCopyScratchSize));
  if (head && crc && head->size == image->size && head->dataCrc32 == *crc && std::memcmp(head->appElfSha256, header.appElfSha256, sizeof(header.appElfSha256)) == 0) {
    stored = head;  // Already on the card: a crash between commit and the log record last time
  } else {
    stored = s_dumps.Store(header, static_cast<uint32_t>(image->size), reader, std::span<uint8_t>(scratch, kCopyScratchSize));
  }

  heap_caps_free(scratch);
  Detail::UnlockCoredump();

  if (!stored) {
    OS_LOGE(TAG, "Failed to copy the crash dump to the SD card; it stays in flash and is retried at the next mount");
    return;
  }

  addDumpStored(*stored, static_cast<uint64_t>(OpenShock::millis()));
  if (!flushRing()) {
    return;
  }

  OS_LOGI(TAG, "Crash dump stored on the SD card (object %llu)", static_cast<unsigned long long>(stored->seq));
  Detail::MarkCoredumpDelivered(Detail::SinkSdCard);
}

static void mount(const Geometry& geometry, MountReason reason)
{
  auto head = RecoverHead(s_card, geometry, [] {
    OS_LOGW(TAG, "SD card log ring is damaged, scanning the whole ring (this can take minutes)");
    s_state.store(State::Recovering);
  });
  if (!head || !s_dumps.Open(s_card, geometry)) {
    OS_LOGE(TAG, "Failed to read the SD card");
    shutDownCard(State::Unusable);
    return;
  }
  if (head->fullScan) {
    OS_LOGW(TAG, "SD card ring recovered by full scan");
  }

  // The boot counter carries on when the card that was removed is put back during the same boot.
  uint32_t headBoot = head->empty ? 0 : head->boot + 1;
  uint32_t boot     = reason == MountReason::Reinserted && s_mountedThisBoot && geometry.epoch == s_lastEpoch ? s_lastBoot : headBoot;

  s_geometry = geometry;
  s_ring.Open(s_card, geometry, *head, boot);

  uint64_t now    = static_cast<uint64_t>(OpenShock::millis());
  uint8_t data[4] = {static_cast<uint8_t>(reason), 0, 0, 0};
  addRecord([&] { return s_ring.Add(RecordType::Mount, data, now); });
  if (!s_mountedThisBoot || boot != s_lastBoot || geometry.epoch != s_lastEpoch) {
    addBootRecord(now);
  }
  if (s_haveTime) {
    addTimeSync(s_syncUnixUs + (static_cast<int64_t>(now) - s_syncUptimeMs) * 1000, TimeSource::Backend, now);
  }
  if (!flushRing()) {
    return;
  }

  s_mountedThisBoot = true;
  s_lastEpoch       = geometry.epoch;
  s_lastBoot        = boot;
  s_state.store(State::Mounted);
  OS_LOGI(TAG, "SD card mounted: %lu ring blocks, next block %llu, boot %lu", static_cast<unsigned long>(geometry.ringBlocks), static_cast<unsigned long long>(s_ring.NextSeq()), static_cast<unsigned long>(boot));

  copyCoredump();
}

// Insertion: initialise, classify, and mount only a card we formatted.
static void insert(MountReason reason)
{
  if (!s_card.Init()) {
    shutDownCard(State::Unusable);
    return;
  }
  if (s_card.SectorCount() < kMinCardSectors) {
    OS_LOGW(TAG, "SD card is smaller than 1 GiB, not using it");
    shutDownCard(State::Unusable);
    return;
  }

  Classification cls = Classify(s_card);
  switch (cls.cardClass) {
    case CardClass::Ours:
      mount(cls.geometry, reason);
      break;
    case CardClass::Blank:
    case CardClass::Foreign:
      // Left initialised for a format; never written until then.
      s_state.store(State::NeedsFormat);
      OS_LOGW(TAG, "SD card (%lu MiB, %s) is not an OpenShock card; format it with `sd format --confirm` to use it", static_cast<unsigned long>(s_card.SectorCount() / 2048), cls.cardClass == CardClass::Blank ? "blank" : "foreign");
      break;
    case CardClass::IoError:
      OS_LOGE(TAG, "Failed to read the SD card");
      shutDownCard(State::Unusable);
      break;
  }
}

static bool format()
{
  if (s_state.load() == State::NoCard) {
    return false;
  }

  s_ring.Close();
  s_dumps.Close();
  if (!s_card.Ready() && !s_card.Init()) {
    shutDownCard(State::Unusable);
    return false;
  }

  FormatInfo info {};
  do {
    info.epoch = esp_random();
  } while (info.epoch == 0 || info.epoch == UINT32_MAX);
  info.diskSignature = esp_random();
  time_t now         = time(nullptr);
  info.formatUnixS   = now > 1'600'000'000 ? static_cast<uint64_t>(now) : 0;  // Only a clock that was ever set
  esp_efuse_mac_get_default(info.mac);
  info.fwVersion = Constants::FW_VERSION;
  info.board     = Board::Name();

  OS_LOGW(TAG, "Formatting the SD card (%lu MiB)", static_cast<unsigned long>(s_card.SectorCount() / 2048));
  if (!Format(s_card, info)) {
    OS_LOGE(TAG, "Formatting the SD card failed");
    shutDownCard(State::Unusable);
    return false;
  }

  Classification cls = Classify(s_card);
  if (cls.cardClass != CardClass::Ours) {
    OS_LOGE(TAG, "Formatted SD card does not read back as formatted");
    shutDownCard(State::Unusable);
    return false;
  }

  mount(cls.geometry, MountReason::Reinserted);
  return s_state.load() == State::Mounted;
}

static bool eject()
{
  if (s_state.load() != State::Mounted) {
    return false;
  }

  uint64_t now = static_cast<uint64_t>(OpenShock::millis());
  addRecord([&] { return s_ring.Add(RecordType::Unmount, {}, now); });
  bool ok = flushRing();
  if (ok) {
    shutDownCard(State::Ejected);
    OS_LOGI(TAG, "SD card can be removed");
  }
  return ok;
}

static void processItem(const uint8_t* item, std::size_t size)
{
  QueuedHeader header;
  if (size < sizeof(header)) {
    return;
  }
  std::memcpy(&header, item, sizeof(header));

  addDroppedIfAny(header.uptimeMs);

  if (header.kind == QueuedKind::TimeSync && size >= sizeof(header) + sizeof(QueuedTimeSync)) {
    QueuedTimeSync sync;
    std::memcpy(&sync, item + sizeof(header), sizeof(sync));
    s_haveTime     = true;
    s_syncUnixUs   = sync.unixUs;
    s_syncUptimeMs = static_cast<int64_t>(header.uptimeMs);
    addTimeSync(sync.unixUs, static_cast<TimeSource>(sync.source), header.uptimeMs);
    return;
  }

  if (header.kind != QueuedKind::Log || size < sizeof(header) + header.tagLen) {
    return;
  }
  std::string_view tag(reinterpret_cast<const char*>(item + sizeof(header)), header.tagLen);
  std::string_view message(reinterpret_cast<const char*>(item + sizeof(header) + header.tagLen), size - sizeof(header) - header.tagLen);
  if (!addRecord([&] { return s_ring.AddLog(header.level, tag, message, header.uptimeMs); })) {
    return;
  }

  // Errors go to the card right away: they are what a crash investigation needs most.
  if (header.level == 1) {
    flushRing();
  }
}

// Debounced card detect: a level must hold for 50 ms to count.
static void pollCardDetect()
{
  static bool s_lastRaw       = false;
  static int64_t s_rawSinceMs = 0;

  bool raw    = readCardDetect();
  int64_t now = OpenShock::millis();
  if (raw != s_lastRaw) {
    s_lastRaw    = raw;
    s_rawSinceMs = now;
    return;
  }
  if (raw == s_cardPresent || now - s_rawSinceMs < kDebounceMs) {
    return;
  }

  s_cardPresent = raw;
  if (raw) {
    OS_LOGI(TAG, "SD card inserted");
    insert(MountReason::Reinserted);
  } else {
    // Surprise removal: stop at once. Queued records stay queued; what the block buffer held is gone.
    if (s_state.load() != State::Ejected) {
      OS_LOGW(TAG, "SD card removed without ejecting it first");
    }
    shutDownCard(State::NoCard);
  }
}

static void handleRequest()
{
  uint32_t request = s_request.exchange(RequestNone);
  if (request == RequestNone) {
    return;
  }

  bool result = false;
  switch (request) {
    case RequestFormat:
      result = format();
      break;
    case RequestEject:
      result = eject();
      break;
    case RequestFlush:
      result = flushRing();
      break;
    default:
      break;
  }

  s_requestResult.store(result);
  xSemaphoreGive(s_requestDone);
}

static void storageTask(void*)
{
  for (;;) {
    handleRequest();
    pollCardDetect();

    if (s_state.load() != State::Mounted) {
      // Records stay queued until a card is mounted; the producers count what no longer fits.
      TaskUtils::WaitForNotify(kPollTicks);
      continue;
    }

    std::size_t size = 0;
    auto* item       = static_cast<uint8_t*>(xRingbufferReceive(s_queue, &size, kPollTicks));
    if (item != nullptr) {
      processItem(item, size);
      vRingbufferReturnItem(s_queue, item);
    }

    if (s_state.load() != State::Mounted) {
      continue;
    }

    int64_t now = OpenShock::millis();
    if (s_haveTime && now - s_lastSyncWrittenMs >= kTimeSyncIntervalMs) {
      addTimeSync(s_syncUnixUs + (now - s_syncUptimeMs) * 1000, TimeSource::Backend, static_cast<uint64_t>(now));
    }
    if (s_ring.HasPending() && now - static_cast<int64_t>(s_ring.OldestPendingUptimeMs()) >= kFlushIntervalMs) {
      flushRing();
    }
  }
}

static bool submit(Request request, TickType_t timeout)
{
  if (s_task == nullptr || xSemaphoreTake(s_requestMutex, timeout) != pdTRUE) {
    return false;
  }

  xSemaphoreTake(s_requestDone, 0);  // A late completion of a request that timed out
  s_request.store(request);
  TaskUtils::Notify(s_task);
  bool done = xSemaphoreTake(s_requestDone, timeout) == pdTRUE;

  xSemaphoreGive(s_requestMutex);
  return done && s_requestResult.load();
}

bool SdCard::Format()
{
  return submit(RequestFormat, portMAX_DELAY);
}

bool SdCard::Eject()
{
  return submit(RequestEject, pdMS_TO_TICKS(5000));
}

static void shutdownHandler()
{
  submit(RequestFlush, kShutdownFlushTicks);
}

SdCard::Info SdCard::GetInfo()
{
  // Read without the storage task's cooperation: a snapshot that can be a record or two stale.
  return Info {
    .state          = s_state.load(),
    .cardSectors    = s_card.SectorCount(),
    .epoch          = s_geometry.epoch,
    .nextSeq        = s_ring.NextSeq(),
    .boot           = s_ring.Boot(),
    .droppedRecords = s_droppedRecordsTotal.load(),
    .droppedBytes   = s_droppedBytesTotal.load(),
    .nextDumpSeq    = s_dumps.NextSeq(),
  };
}

void Detail::SdCardInit()
{
  static_assert(CONFIG_OPENSHOCK_DIAG_SDCARD_CD_GPIO >= 0, "The SD card needs a card-detect pin (CONFIG_OPENSHOCK_DIAG_SDCARD_CD_GPIO)");

  if (!SdHostCard::PinsConfigured()) {
    OS_LOGE(TAG, "SD card support is built in but its pins are not configured");
    return;
  }

  s_queue        = xRingbufferCreate(CONFIG_OPENSHOCK_DIAG_SDCARD_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
  s_requestMutex = xSemaphoreCreateMutex();
  s_requestDone  = xSemaphoreCreateBinary();
  if (s_queue == nullptr || s_requestMutex == nullptr || s_requestDone == nullptr) {
    OS_LOGE(TAG, "Out of memory for the SD card log queue");
    return;
  }

  gpio_config_t cd = {};
  cd.pin_bit_mask  = 1ULL << CONFIG_OPENSHOCK_DIAG_SDCARD_CD_GPIO;
  cd.mode          = GPIO_MODE_INPUT;
#if CONFIG_OPENSHOCK_DIAG_SDCARD_CD_ACTIVE_LOW
  cd.pull_up_en = GPIO_PULLUP_ENABLE;
#else
  cd.pull_down_en = GPIO_PULLDOWN_ENABLE;
#endif
  gpio_config(&cd);

  s_state.store(State::NoCard);

  // The mount at boot, and the coredump copy, run here, before WiFi starts.
  s_cardPresent = readCardDetect();
  if (s_cardPresent) {
    insert(MountReason::Boot);
  } else {
    OS_LOGI(TAG, "No SD card inserted");
  }

  if (TaskUtils::TaskCreateUniversal(storageTask, "SdStorage", 6144, nullptr, 1, &s_task, tskNO_AFFINITY) != pdPASS) {
    OS_LOGE(TAG, "Failed to start the SD card storage task");
    shutDownCard(State::Failed);
    return;
  }

  esp_register_shutdown_handler(&shutdownHandler);
}
