#include <freertos/FreeRTOS.h>

#include "diagnostics/Diagnostics.h"

const char* const TAG = "Diagnostics";

#include "Coredump.h"
#include "Internal.h"
#include "Logging.h"
#include "LogTap.h"
#include "SimpleMutex.h"

#include <esp_log.h>
#include <esp_system.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

using namespace OpenShock;
using namespace OpenShock::Diagnostics;

// Built only with uploading (CONFIG_OPENSHOCK_DIAG_UPLOAD, Upload.cpp) or the SD card (CONFIG_OPENSHOCK_DIAG_SDCARD,
// sdcard/SdStorage.cpp) compiled in: routes log lines to them and keeps track of which of them has the coredump.

static constexpr std::size_t kMaxLineLen = 256;

static vprintf_like_t s_idfVprintf = nullptr;

static SimpleMutex s_coredumpMutex;
static std::atomic<bool> s_coredumpStored {false};
static std::atomic<uint8_t> s_coredumpDelivered {0};
static std::size_t s_coredumpSize = 0;
static uint32_t s_coredumpCrc     = 0;

// ---------------------------------------------------------------------------------------------------------------------
// Log lines

static void dispatch(char level, const char* tag, std::size_t tagLen, const char* message, std::size_t len)
{
#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  Detail::UploadCaptureLog(level, tag, tagLen, message, len);
#endif
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

static uint8_t requiredSinks()
{
  uint8_t required = 0;
#if CONFIG_OPENSHOCK_DIAG_SDCARD
  if (Detail::SdCardConfigured()) {
    required |= Detail::SinkSdCard;
  }
#endif
#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  if (Detail::CoredumpUploadEnabled()) {
    required |= Detail::SinkBackend;
  }
#endif
  return required;
}

static void eraseIfDelivered()
{
  // With no sink it stays in the partition, where the serial console and esptool can still read it.
  uint8_t required = requiredSinks();
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
#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  if (sink == SinkBackend) {
    RememberCoredumpUploaded(s_coredumpCrc, s_coredumpSize);
  }
#endif
  s_coredumpDelivered.fetch_or(sink);
  eraseIfDelivered();
}

bool Detail::CoredumpAwaitingBackend()
{
  return s_coredumpStored.load() && (s_coredumpDelivered.load() & SinkBackend) == 0;
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

#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  if (Detail::CoredumpUploadedBefore(s_coredumpCrc, s_coredumpSize)) {
    s_coredumpDelivered.fetch_or(Detail::SinkBackend);
  }
#endif

  std::string reason = Coredump::PanicReason();
  OS_LOGW(TAG, "A crash dump from an earlier boot is stored (%zu bytes)%s%s", image->size, reason.empty() ? "" : ": ", reason.c_str());
}

// ---------------------------------------------------------------------------------------------------------------------
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

Diagnostics::Status Diagnostics::GetStatus()
{
  uint8_t delivered = s_coredumpDelivered.load();
  Status status {
    .coredumpStored = s_coredumpStored.load(),
    .coredumpSize   = s_coredumpSize,
    .coredumpOnCard = (delivered & Detail::SinkSdCard) != 0,
  };
#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  status.coredumpUploaded = (delivered & Detail::SinkBackend) != 0;
  Detail::UploadStatus(status);
#endif
  return status;
}

void Diagnostics::Update(Util::FunctionRef<bool()> isBackendOnline)
{
#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  Detail::UploadUpdate(isBackendOnline);
#endif
}

bool Diagnostics::Init()
{
#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  Detail::UploadInit();
#endif

  Logging::SetTap(&logTap);
  s_idfVprintf = esp_log_set_vprintf(&idfLogHook);

  findCoredump();

#if CONFIG_OPENSHOCK_DIAG_SDCARD
  Detail::SdCardInit();
#endif

  eraseIfDelivered();
  return true;
}
