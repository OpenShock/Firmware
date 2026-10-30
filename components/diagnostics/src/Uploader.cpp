#include <freertos/FreeRTOS.h>

#include "Internal.h"

const char* const TAG = "DiagUpload";

#include "BoardDescriptor.h"
#include "config/Config.h"
#include "Coredump.h"
#include "diagnostics/Diagnostics.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"
#include "OpenShock.h"

#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <string>

using namespace OpenShock;
using namespace OpenShock::Diagnostics;

// Upload endpoints, on CONFIG_OPENSHOCK_DIAG_UPLOAD_DOMAIN. When that is the backend the hub is linked to they take the
// hub token in the DeviceToken header, like the other /device endpoints; elsewhere they get no token.
static constexpr std::string_view kCoredumpPath = "/1/device/diagnostics/coredump";  // application/octet-stream: the raw ELF coredump
static constexpr std::string_view kLogsPath     = "/1/device/diagnostics/logs";      // text/plain: log lines, oldest first

static constexpr std::array<uint16_t, 4> kAcceptedCodes = {200, 201, 202, 204};
static constexpr uint32_t kCoredumpTimeoutMs            = 60'000;
static constexpr uint32_t kLogsTimeoutMs                = 20'000;

static bool sameDomain(std::string_view a, std::string_view b)
{
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}

struct Target {
  std::string uri;
  std::map<std::string, std::string> headers;
};

// Where an upload goes, with the headers every upload carries: who is sending, and what it runs. The linked backend, or
// the override domain; the hub token only goes to the linked backend.
// Where an upload goes, with the headers every upload carries: who is sending, and what it runs. The hub token goes
// along only when the upload domain is the backend the hub is linked to.
static std::optional<Target> uploadTarget(std::string_view path, const char* contentType, const char*& result)
{
  constexpr std::string_view domain = CONFIG_OPENSHOCK_DIAG_UPLOAD_DOMAIN;

  Target target;
  target.uri     = "https://" + std::string(domain) + std::string(path);
  target.headers = {
    {            "Content-Type",                                 contentType},
    {    "X-OpenShock-Firmware",                       Constants::FW_VERSION},
    {       "X-OpenShock-Board",                  std::string(Board::Name())},
    {        "X-OpenShock-Chip",                  std::string(Board::Chip())},
    {"X-OpenShock-Reset-Reason", Detail::ResetReasonName(esp_reset_reason())},
  };

  auto backend = Config::GetBackendDomain();
  if (backend && sameDomain(domain, *backend)) {
    if (auto token = Config::GetBackendAuthToken()) {
      target.headers["DeviceToken"] = std::move(*token);
    }
  }
  return target;
}

static bool uploadCoredump(const char*& result)
{
  Detail::LockCoredump();

  auto image = Coredump::Find();
  if (!image) {
    Detail::UnlockCoredump();
    return true;  // Erased meanwhile
  }

  auto target = uploadTarget(kCoredumpPath, "application/octet-stream", result);
  if (!target) {
    Detail::UnlockCoredump();
    return false;
  }
  auto& headers = target->headers;

  // The dump names the build that crashed (its ELF SHA-256), which after an OTA update is not the running one.
  if (auto summary = Coredump::GetSummary()) {
    headers["X-OpenShock-Crash-Elf-Sha256"] = summary->elfSha256;
    headers["X-OpenShock-Crash-Task"]       = summary->excTask;
  }
  if (std::string reason = Coredump::PanicReason(); !reason.empty()) {
    headers["X-OpenShock-Panic-Reason"] = std::move(reason);
  }

  HTTP::Client client;
  auto response = client.Upload(target->uri, headers, image->size, [&image](std::size_t offset, uint8_t* buffer, std::size_t capacity) -> int {
    return Coredump::Read(*image, static_cast<uint32_t>(offset), buffer, capacity) ? static_cast<int>(capacity) : -1;
  }, kAcceptedCodes, kCoredumpTimeoutMs);
  Detail::UnlockCoredump();

  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGW(TAG, "Crash dump upload failed: %s (%d)", response.ResultToString(), response.code);
    result = "crash dump upload failed";
    return false;
  }

  OS_LOGI(TAG, "Uploaded the crash dump (%zu bytes)", image->size);
  Detail::MarkCoredumpDelivered(Detail::SinkBackend);
  return true;
}

static bool uploadLogs(const char*& result)
{
  const std::size_t capacity = CONFIG_OPENSHOCK_DIAG_LOG_BUFFER_SIZE;
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[capacity + 64]);
  if (buffer == nullptr) {
    result = "out of memory";
    return false;
  }

  // Lines lost to the ring overflowing are owned up to at the top of the upload.
  uint64_t consumed = Detail::LogConsumed();
  uint64_t from     = 0;
  std::size_t len   = Detail::PeekLog(consumed, buffer.get() + 64, capacity, from);
  if (len == 0) {
    return true;
  }

  char* body       = buffer.get() + 64;
  std::size_t size = len;
  if (from > consumed) {
    int n = snprintf(buffer.get(), 64, "[... %llu bytes lost ...]\r\n", static_cast<unsigned long long>(from - consumed));
    n     = std::clamp(n, 0, 63);
    body -= n;
    std::memmove(body, buffer.get(), static_cast<std::size_t>(n));
    size += static_cast<std::size_t>(n);
  }

  auto target = uploadTarget(kLogsPath, "text/plain; charset=utf-8", result);
  if (!target) {
    return false;
  }

  HTTP::Client client;
  auto response = client.Upload(target->uri, target->headers, size, [body, size](std::size_t offset, uint8_t* out, std::size_t outCapacity) -> int {
    std::size_t n = std::min(outCapacity, size - offset);
    std::memcpy(out, body + offset, n);
    return static_cast<int>(n);
  }, kAcceptedCodes, kLogsTimeoutMs);

  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGW(TAG, "Log upload failed: %s (%d)", response.ResultToString(), response.code);
    result = "log upload failed";
    return false;
  }

  Detail::ConsumeLog(from + len);
  return true;
}

bool Detail::RunUploads(const char*& result)
{
  result  = "ok";
  bool ok = true;

  if (CoredumpUploadEnabled() && CoredumpAwaitingBackend()) {
    ok = uploadCoredump(result) && ok;
  }
  if (GetStatus().pendingLogBytes > 0) {
    ok = uploadLogs(result) && ok;
  }

  return ok;
}
