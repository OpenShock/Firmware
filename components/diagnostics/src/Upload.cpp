#include <freertos/FreeRTOS.h>

#include "Internal.h"

const char* const TAG = "DiagUpload";

#include "config/Config.h"
#include "diagnostics/Diagnostics.h"
#include "enums/DiagnosticsLogLevel.h"
#include "Logging.h"
#include "OpenShock.h"
#include "RetainedRing.h"
#include "Temporal.h"
#include "util/TaskUtils.h"

#include <esp_attr.h>
#include <esp_system.h>
#include <nvs.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <utility>

using namespace OpenShock;
using namespace OpenShock::Diagnostics;

// Built only with CONFIG_OPENSHOCK_DIAG_UPLOAD: the upload settings, the retained log ring, and the upload task. The
// requests themselves are in Uploader.cpp.

static constexpr const char* kNvsNamespace    = "diag";
static constexpr const char* kKeyUploadedCrc  = "core_crc";  // u32: CRC of the last coredump the backend accepted
static constexpr const char* kKeyUploadedSize = "core_len";  // u32: and its size

static constexpr int64_t kUpdateIntervalMs    = 1000;
static constexpr int64_t kLogUploadIntervalMs = 60 * 1000;  // Batch log lines: at most one log upload a minute
static constexpr int64_t kMinRetryMs          = 60 * 1000;
static constexpr int64_t kMaxRetryMs          = 60 * 60 * 1000;
static constexpr std::size_t kMaxLineLen      = 256;

// Log ring: in memory a software reset (panic, watchdog, esp_restart) leaves alone, so the lines that led up to a
// crash are still there for the next boot to upload. RetainedRing::Adopt() tells it from the garbage after a power-up.
static __NOINIT_ATTR RetainedRingState s_logRingState;
static __NOINIT_ATTR char s_logRingData[CONFIG_OPENSHOCK_DIAG_LOG_BUFFER_SIZE];
static RetainedRing s_logRing(s_logRingState, s_logRingData, sizeof(s_logRingData));
static portMUX_TYPE s_logRingLock = portMUX_INITIALIZER_UNLOCKED;  // Logging runs on every task

static std::atomic<bool> s_coredumpUploadEnabled {false};
static std::atomic<uint8_t> s_logUploadLevel {std::to_underlying(DiagnosticsLogLevel::None)};
static std::atomic<TaskHandle_t> s_uploadTask {nullptr};

static std::atomic<bool> s_uploadRunning {false};
static std::atomic<bool> s_uploadRequested {false};
static std::atomic<const char*> s_lastUploadResult {nullptr};
static int64_t s_nextUpdateMs    = 0;
static int64_t s_retryAtMs       = 0;
static int64_t s_nextLogUploadMs = 0;
static uint32_t s_failures       = 0;

// ---------------------------------------------------------------------------------------------------------------------
// Settings. Both uploads are off until the owner turns them on. NVS (namespace "diag") only remembers which coredump
// the backend already has.

static bool nvsGetU32(const char* key, uint32_t& value)
{
  nvs_handle_t handle;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  bool ok = nvs_get_u32(handle, key, &value) == ESP_OK;
  nvs_close(handle);
  return ok;
}

static bool nvsSetU32(const char* key, uint32_t value)
{
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
      err = nvs_commit(handle);
    }
    nvs_close(handle);
  }
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to save %s: %s", key, esp_err_to_name(err));
    return false;
  }
  return true;
}

// The opt-ins live in the config (Config::DiagnosticsConfig). Cached here, because the log tap reads the level on every
// line, and refreshed from the config on every Update().
static void refreshSettings()
{
  if (auto config = Config::GetDiagnosticsConfig()) {
    s_coredumpUploadEnabled.store(config->coredumpUpload, std::memory_order_relaxed);
    s_logUploadLevel.store(std::to_underlying(config->logUploadLevel), std::memory_order_relaxed);
  }
}

bool Detail::CoredumpUploadEnabled()
{
  return s_coredumpUploadEnabled.load(std::memory_order_relaxed);
}

static bool logUploadEnabled()
{
  return s_logUploadLevel.load(std::memory_order_relaxed) != std::to_underlying(DiagnosticsLogLevel::None);
}

// ---------------------------------------------------------------------------------------------------------------------
// Capture

static void captureLine(char level, const char* tag, std::size_t tagLen, const char* message, std::size_t len)
{
  char line[kMaxLineLen];
  int n = snprintf(line, sizeof(line), "[%lld][%c][%.*s] %.*s\r\n", static_cast<long long>(OpenShock::millis()), level, static_cast<int>(tagLen), tag, static_cast<int>(len), message);
  if (n <= 0) {
    return;
  }
  std::size_t size = static_cast<std::size_t>(n);
  if (size >= sizeof(line)) {
    size = sizeof(line) - 1;
    std::memcpy(line + size - 2, "\r\n", 2);  // Keep the line ending of a cut line
  }

  portENTER_CRITICAL(&s_logRingLock);
  s_logRing.Append(line, size);
  portEXIT_CRITICAL(&s_logRingLock);
}

// Severity of a log letter on the DiagnosticsLogLevel scale; N (always printed) counts as info, a panic as an error.
static uint8_t levelRank(char letter)
{
  switch (letter) {
    case 'C':
    case 'E':
      return 1;
    case 'W':
      return 2;
    case 'I':
    case 'N':
      return 3;
    case 'D':
      return 4;
    default:
      return 5;
  }
}

void Detail::UploadCaptureLog(char level, const char* tag, std::size_t tagLen, const char* message, std::size_t len)
{
  // Errors are always kept, for an upload turned on later; the upload task's own lines (HTTP, TLS, results) never are,
  // or every upload would leave something to upload.
  uint8_t keep = std::max<uint8_t>(s_logUploadLevel.load(std::memory_order_relaxed), 1);
  if (levelRank(level) <= keep && xTaskGetCurrentTaskHandle() != s_uploadTask.load(std::memory_order_relaxed)) {
    captureLine(level, tag, tagLen, message, len);
  }
}

std::size_t Detail::PeekLog(uint64_t start, char* out, std::size_t outCapacity, uint64_t& from)
{
  portENTER_CRITICAL(&s_logRingLock);
  std::size_t len = s_logRing.Peek(start, out, outCapacity, from);
  portEXIT_CRITICAL(&s_logRingLock);
  return len;
}

uint64_t Detail::LogConsumed()
{
  portENTER_CRITICAL(&s_logRingLock);
  uint64_t consumed = s_logRing.Consumed();
  portEXIT_CRITICAL(&s_logRingLock);
  return consumed;
}

void Detail::ConsumeLog(uint64_t upTo)
{
  portENTER_CRITICAL(&s_logRingLock);
  s_logRing.Consume(upTo);
  portEXIT_CRITICAL(&s_logRingLock);
}

static uint64_t pendingLogBytes()
{
  portENTER_CRITICAL(&s_logRingLock);
  uint64_t pending = s_logRing.Pending();
  portEXIT_CRITICAL(&s_logRingLock);
  return pending;
}

// ---------------------------------------------------------------------------------------------------------------------
// Coredump delivery, remembered across reboots so a dump waiting for another sink isn't uploaded again.

bool Detail::CoredumpUploadedBefore(uint32_t crc, std::size_t size)
{
  uint32_t uploadedCrc = 0, uploadedSize = 0;
  return nvsGetU32(kKeyUploadedCrc, uploadedCrc) && nvsGetU32(kKeyUploadedSize, uploadedSize) && uploadedCrc == crc && uploadedSize == size;
}

void Detail::RememberCoredumpUploaded(uint32_t crc, std::size_t size)
{
  nvsSetU32(kKeyUploadedCrc, crc);
  nvsSetU32(kKeyUploadedSize, static_cast<uint32_t>(size));
}

// ---------------------------------------------------------------------------------------------------------------------
// Upload task

static bool isAbnormalReset(esp_reset_reason_t reason)
{
  switch (reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
      return true;
    default:
      return false;
  }
}

static void uploadTask(void*)
{
  s_uploadTask.store(xTaskGetCurrentTaskHandle());
  const char* result = nullptr;
  bool ok            = Detail::RunUploads(result);

  int64_t now = OpenShock::millis();
  if (ok) {
    s_failures        = 0;
    s_retryAtMs       = 0;
    s_nextLogUploadMs = now + kLogUploadIntervalMs;
  } else {
    s_failures++;
    s_retryAtMs = now + std::min(kMaxRetryMs, kMinRetryMs << std::min<uint32_t>(s_failures - 1, 6));
  }
  s_lastUploadResult.store(result);
  s_uploadTask.store(nullptr);
  s_uploadRunning.store(false);

  vTaskDelete(nullptr);
}

void Diagnostics::RequestUpload()
{
  s_uploadRequested.store(true);
}

void Detail::UploadUpdate(Util::FunctionRef<bool()> isBackendOnline)
{
  int64_t now = OpenShock::millis();
  if (now < s_nextUpdateMs || s_uploadRunning.load()) {
    return;
  }
  s_nextUpdateMs = now + kUpdateIntervalMs;
  refreshSettings();

  bool forced = s_uploadRequested.load();
  if (!forced && now < s_retryAtMs) {
    return;
  }

  bool coredumpDue = s_coredumpUploadEnabled.load() && Detail::CoredumpAwaitingBackend();
  bool logsDue     = logUploadEnabled() && pendingLogBytes() > 0 && (forced || now >= s_nextLogUploadMs);
  if (!coredumpDue && !logsDue) {
    s_uploadRequested.store(false);
    return;
  }
  if (!isBackendOnline()) {
    return;
  }

  s_uploadRequested.store(false);
  s_uploadRunning.store(true);
  // TLS handshake plus the HTTP client; the OTA task, doing the same, measured 6.2 KB.
  if (TaskUtils::TaskCreateExpensive(uploadTask, "DiagUpload", 8192, nullptr, 1, nullptr) != pdPASS) {
    OS_LOGE(TAG, "Failed to start the upload task");
    s_uploadRunning.store(false);
  }
}

void Detail::UploadStatus(Status& status)
{
  status.pendingLogBytes  = pendingLogBytes();
  status.uploadRunning    = s_uploadRunning.load();
  status.lastUploadResult = s_lastUploadResult.load();
}

void Detail::UploadInit()
{
  // Still the only task: no lock needed yet.
  bool kept = s_logRing.Adopt();

  refreshSettings();

  esp_reset_reason_t reason = esp_reset_reason();
  if (isAbnormalReset(reason)) {
    char marker[96];
    int n = snprintf(marker, sizeof(marker), "previous boot ended by %s (firmware %s)", ResetReasonName(reason), Constants::FW_VERSION);
    captureLine('C', "boot", 4, marker, static_cast<std::size_t>(std::max(n, 0)));
  }
  OS_LOGI(TAG, "Reset reason: %s; %llu log bytes %s", ResetReasonName(reason), static_cast<unsigned long long>(pendingLogBytes()), kept ? "carried over" : "(log ring reset)");
}
