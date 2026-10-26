#include <freertos/FreeRTOS.h>

#include "diagnostics/Diagnostics.h"

const char* const TAG = "Diagnostics";

#include "Coredump.h"
#include "Internal.h"
#include "Logging.h"
#include "LogTap.h"
#include "OpenShock.h"
#include "RetainedRing.h"
#include "SimpleMutex.h"
#include "Temporal.h"
#include "util/TaskUtils.h"

#include <esp_attr.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>

using namespace OpenShock;
using namespace OpenShock::Diagnostics;

static constexpr const char* kNvsNamespace    = "diag";
static constexpr const char* kKeyCoredump     = "up_core";    // u8: coredump upload enabled
static constexpr const char* kKeyLogLevel     = "log_lvl";    // u32: LogLevel uploaded, 0 = none
static constexpr const char* kKeyDomain       = "up_domain";  // str: upload domain override, absent = linked backend
static constexpr const char* kKeyUploadedCrc  = "core_crc";   // u32: CRC of the last coredump the backend accepted
static constexpr const char* kKeyUploadedSize = "core_len";   // u32: and its size

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

static vprintf_like_t s_idfVprintf = nullptr;

static std::atomic<bool> s_coredumpUploadEnabled {false};
static std::atomic<uint8_t> s_logUploadLevel {static_cast<uint8_t>(LogLevel::None)};
static SimpleMutex s_domainMutex;
static std::string s_uploadDomain;  // Guarded by s_domainMutex
static std::atomic<TaskHandle_t> s_uploadTask {nullptr};

static SimpleMutex s_coredumpMutex;
static std::atomic<bool> s_coredumpStored {false};
static std::atomic<uint8_t> s_coredumpDelivered {0};
static std::size_t s_coredumpSize = 0;
static uint32_t s_coredumpCrc     = 0;

static std::atomic<bool> s_uploadRunning {false};
static std::atomic<bool> s_uploadRequested {false};
static std::atomic<const char*> s_lastUploadResult {nullptr};
static int64_t s_nextUpdateMs    = 0;
static int64_t s_retryAtMs       = 0;
static int64_t s_nextLogUploadMs = 0;
static uint32_t s_failures       = 0;

// ---------------------------------------------------------------------------------------------------------------------
// Settings (NVS, namespace "diag"). Both uploads are off until the owner turns them on.

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

static std::string nvsGetString(const char* key)
{
  nvs_handle_t handle;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return {};
  }
  std::string value;
  std::size_t len = 0;
  if (nvs_get_str(handle, key, nullptr, &len) == ESP_OK && len > 1) {
    value.resize(len);
    if (nvs_get_str(handle, key, value.data(), &len) == ESP_OK) {
      value.resize(len - 1);
    } else {
      value.clear();
    }
  }
  nvs_close(handle);
  return value;
}

// Stores `value`, or erases the key for an empty one.
static bool nvsSetString(const char* key, const std::string& value)
{
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = value.empty() ? nvs_erase_key(handle, key) : nvs_set_str(handle, key, value.c_str());
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      err = ESP_OK;
    }
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

static bool loadFlag(const char* key)
{
  uint32_t value = 0;
  return nvsGetU32(key, value) && value != 0;
}

bool Diagnostics::IsCoredumpUploadEnabled()
{
  return s_coredumpUploadEnabled.load(std::memory_order_relaxed);
}

bool Diagnostics::SetCoredumpUploadEnabled(bool enabled)
{
  if (!nvsSetU32(kKeyCoredump, enabled ? 1 : 0)) {
    return false;
  }
  s_coredumpUploadEnabled.store(enabled, std::memory_order_relaxed);
  return true;
}

static constexpr const char* kLogLevelNames[] = {"none", "error", "warn", "info", "debug", "verbose"};

const char* Diagnostics::LogLevelName(LogLevel level)
{
  auto index = static_cast<std::size_t>(level);
  return index < std::size(kLogLevelNames) ? kLogLevelNames[index] : "unknown";
}

bool Diagnostics::TryParseLogLevel(std::string_view name, LogLevel& out)
{
  for (std::size_t i = 0; i < std::size(kLogLevelNames); i++) {
    if (name == kLogLevelNames[i]) {
      out = static_cast<LogLevel>(i);
      return true;
    }
  }
  return false;
}

LogLevel Diagnostics::GetLogUploadLevel()
{
  return static_cast<LogLevel>(s_logUploadLevel.load(std::memory_order_relaxed));
}

bool Diagnostics::SetLogUploadLevel(LogLevel level)
{
  if (level > LogLevel::Verbose || !nvsSetU32(kKeyLogLevel, static_cast<uint32_t>(level))) {
    return false;
  }
  s_logUploadLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
  return true;
}

// A host name or address, optionally with a port: letters, digits, '.', '-', and one ':' before the port.
static bool isValidDomain(std::string_view domain)
{
  if (domain.empty() || domain.size() > 253) {
    return false;
  }
  std::size_t colon     = domain.find(':');
  std::string_view host = domain.substr(0, colon);
  if (host.empty() || host.front() == '.' || host.front() == '-' || host.back() == '.' || host.back() == '-') {
    return false;
  }
  for (char c : host) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-')) {
      return false;
    }
  }
  if (colon == std::string_view::npos) {
    return true;
  }
  std::string_view port = domain.substr(colon + 1);
  if (port.empty() || port.size() > 5) {
    return false;
  }
  for (char c : port) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

std::string Diagnostics::GetUploadDomain()
{
  s_domainMutex.lock(portMAX_DELAY);
  std::string domain = s_uploadDomain;
  s_domainMutex.unlock();
  return domain;
}

bool Diagnostics::SetUploadDomain(std::string_view domain)
{
  if (!domain.empty() && !isValidDomain(domain)) {
    return false;
  }
  std::string value(domain);
  if (!nvsSetString(kKeyDomain, value)) {
    return false;
  }
  s_domainMutex.lock(portMAX_DELAY);
  s_uploadDomain = std::move(value);
  s_domainMutex.unlock();
  return true;
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

// Severity of a log letter on the LogLevel scale; N (always printed) counts as info, a panic as an error.
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

static void dispatch(char level, const char* tag, std::size_t tagLen, const char* message, std::size_t len)
{
  // Errors are always kept, for an upload turned on later; the upload task's own lines (HTTP, TLS, results) never are,
  // or every upload would leave something to upload.
  uint8_t keep = std::max<uint8_t>(s_logUploadLevel.load(std::memory_order_relaxed), 1);
  if (levelRank(level) <= keep && xTaskGetCurrentTaskHandle() != s_uploadTask.load(std::memory_order_relaxed)) {
    captureLine(level, tag, tagLen, message, len);
  }
#if CONFIG_OPENSHOCK_DIAG_SDCARD
  Detail::SdCardEnqueueLog(level, tag, tagLen, message, len);
#endif
}

static void logTap(char level, const char* tag, const char* message, std::size_t len)
{
  dispatch(level, tag, std::strlen(tag), message, len);
}

// ESP-IDF's own log lines: "L (uptime) tag: message", possibly wrapped in ANSI colour codes. Lines that don't look like
// that (continuations, raw printf through the log) are left alone.
static void captureIdfLine(const char* raw, std::size_t rawLen)
{
  char line[kMaxLineLen];
  std::size_t len = 0;
  for (std::size_t i = 0; i < rawLen && len < sizeof(line); i++) {
    if (raw[i] == '\x1b') {
      while (i < rawLen && raw[i] != 'm') {
        i++;
      }
      continue;
    }
    line[len++] = raw[i];
  }
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) {
    len--;
  }

  if (len < 4 || std::strchr("EWIDV", line[0]) == nullptr || line[1] != ' ' || line[2] != '(') {
    return;
  }
  const char* end = line + len;
  const char* p   = static_cast<const char*>(std::memchr(line, ')', len));
  if (p == nullptr || p + 2 > end || p[1] != ' ') {
    return;
  }
  const char* tag = p + 2;
  const char* sep = tag;
  while (sep + 1 < end && !(sep[0] == ':' && sep[1] == ' ')) {
    sep++;
  }
  if (sep + 1 >= end) {
    return;
  }

  const char* message = sep + 2;
  dispatch(line[0], tag, static_cast<std::size_t>(sep - tag), message, static_cast<std::size_t>(end - message));
}

static int idfLogHook(const char* format, va_list args)
{
  char line[kMaxLineLen];
  va_list copy;
  va_copy(copy, args);
  int len = vsnprintf(line, sizeof(line), format, copy);
  va_end(copy);

  int result = s_idfVprintf != nullptr ? s_idfVprintf(format, args) : vprintf(format, args);

  if (len > 0) {
    captureIdfLine(line, std::min(static_cast<std::size_t>(len), sizeof(line) - 1));
  }
  return result;
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
// Coredump

void Detail::LockCoredump()
{
  s_coredumpMutex.lock(portMAX_DELAY);
}

void Detail::UnlockCoredump()
{
  s_coredumpMutex.unlock();
}

static void eraseIfDelivered()
{
  uint8_t required = 0;
  if (Detail::SdCardConfigured()) {
    required |= Detail::SinkSdCard;
  }
  if (s_coredumpUploadEnabled.load(std::memory_order_relaxed)) {
    required |= Detail::SinkBackend;
  }

  // With no sink it stays in the partition, where the serial console and esptool can still read it.
  if (required == 0 || (s_coredumpDelivered.load() & required) != required) {
    return;
  }

  Detail::LockCoredump();
  if (s_coredumpStored.load() && Coredump::Erase()) {
    OS_LOGI(TAG, "Crash dump delivered, erased it from flash");
    s_coredumpStored.store(false);
    s_coredumpDelivered.store(0);
  }
  Detail::UnlockCoredump();
}

void Detail::MarkCoredumpDelivered(CoredumpSink sink)
{
  if (sink == SinkBackend) {
    // Remembered across reboots, so a dump waiting for the card isn't uploaded again.
    nvsSetU32(kKeyUploadedCrc, s_coredumpCrc);
    nvsSetU32(kKeyUploadedSize, static_cast<uint32_t>(s_coredumpSize));
  }
  s_coredumpDelivered.fetch_or(sink);
  eraseIfDelivered();
}

bool Diagnostics::EraseCoredump()
{
  Detail::LockCoredump();
  bool ok = !s_coredumpStored.load() || Coredump::Erase();
  if (ok) {
    s_coredumpStored.store(false);
    s_coredumpDelivered.store(0);
  }
  Detail::UnlockCoredump();
  return ok;
}

static void findCoredump()
{
  auto image = Coredump::Find();
  if (!image) {
    return;
  }

  s_coredumpSize = image->size;
  s_coredumpCrc  = Coredump::Crc(*image).value_or(0);
  s_coredumpStored.store(true);

  uint32_t uploadedCrc = 0, uploadedSize = 0;
  if (nvsGetU32(kKeyUploadedCrc, uploadedCrc) && nvsGetU32(kKeyUploadedSize, uploadedSize) && uploadedCrc == s_coredumpCrc && uploadedSize == image->size) {
    s_coredumpDelivered.fetch_or(Detail::SinkBackend);
  }

  std::string reason = Coredump::PanicReason();
  OS_LOGW(TAG, "A crash dump from an earlier boot is stored (%zu bytes)%s%s", image->size, reason.empty() ? "" : ": ", reason.c_str());
  if (!s_coredumpUploadEnabled.load()) {
    OS_LOGI(TAG, "Crash dump upload is off; enable it with `diag coredump true` to send it to the backend");
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Uploads

const char* Detail::ResetReasonName(int reason)
{
  switch (static_cast<esp_reset_reason_t>(reason)) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

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

bool Diagnostics::UploadsAllowed()
{
  return Constants::FW_OFFICIAL || !GetUploadDomain().empty();
}

void Diagnostics::RequestUpload()
{
  s_uploadRequested.store(true);
}

void Diagnostics::Update(Util::FunctionRef<bool()> isBackendOnline)
{
  int64_t now = OpenShock::millis();
  if (now < s_nextUpdateMs || s_uploadRunning.load()) {
    return;
  }
  s_nextUpdateMs = now + kUpdateIntervalMs;

  bool forced = s_uploadRequested.load();
  if (!forced && now < s_retryAtMs) {
    return;
  }

  bool coredumpDue = s_coredumpUploadEnabled.load() && s_coredumpStored.load() && (s_coredumpDelivered.load() & Detail::SinkBackend) == 0;
  bool logsDue     = GetLogUploadLevel() != LogLevel::None && pendingLogBytes() > 0 && (forced || now >= s_nextLogUploadMs);
  if ((!coredumpDue && !logsDue) || !UploadsAllowed()) {
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

Diagnostics::Status Diagnostics::GetStatus()
{
  uint8_t delivered = s_coredumpDelivered.load();
  return Status {
    .coredumpStored   = s_coredumpStored.load(),
    .coredumpSize     = s_coredumpSize,
    .coredumpUploaded = (delivered & Detail::SinkBackend) != 0,
    .coredumpOnCard   = (delivered & Detail::SinkSdCard) != 0,
    .pendingLogBytes  = pendingLogBytes(),
    .uploadsAllowed   = UploadsAllowed(),
    .uploadRunning    = s_uploadRunning.load(),
    .lastUploadResult = s_lastUploadResult.load(),
  };
}

// ---------------------------------------------------------------------------------------------------------------------

bool Diagnostics::Init()
{
  // Still the only task: no lock needed yet.
  bool kept = s_logRing.Adopt();

  s_coredumpUploadEnabled.store(loadFlag(kKeyCoredump));
  uint32_t level = 0;
  nvsGetU32(kKeyLogLevel, level);
  s_logUploadLevel.store(static_cast<uint8_t>(std::min<uint32_t>(level, static_cast<uint32_t>(LogLevel::Verbose))));
  s_uploadDomain = nvsGetString(kKeyDomain);

  Logging::SetTap(&logTap);
  s_idfVprintf = esp_log_set_vprintf(&idfLogHook);

  esp_reset_reason_t reason = esp_reset_reason();
  if (isAbnormalReset(reason)) {
    char marker[96];
    int n = snprintf(marker, sizeof(marker), "previous boot ended by %s (firmware %s)", Detail::ResetReasonName(reason), Constants::FW_VERSION);
    captureLine('C', "boot", 4, marker, static_cast<std::size_t>(std::max(n, 0)));
  }
  OS_LOGI(TAG, "Reset reason: %s; %llu log bytes %s", Detail::ResetReasonName(reason), static_cast<unsigned long long>(pendingLogBytes()), kept ? "carried over" : "(log ring reset)");

  if (!UploadsAllowed() && (s_coredumpUploadEnabled.load() || GetLogUploadLevel() != LogLevel::None)) {
    OS_LOGW(TAG, "Uploads are turned on but this is not official firmware; nothing is uploaded unless an upload domain is set (`diag domain`)");
  }

  findCoredump();

#if CONFIG_OPENSHOCK_DIAG_SDCARD
  Detail::SdCardInit();
#endif

  eraseIfDelivered();
  return true;
}

#if !CONFIG_OPENSHOCK_DIAG_SDCARD
bool Detail::SdCardConfigured()
{
  return false;
}
#endif
