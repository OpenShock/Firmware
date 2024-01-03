#include "OtaUpdateManager.h"

#include "CaptivePortal.h"
#include "config/Config.h"
#include "Constants.h"
#include "GatewayConnectionManager.h"
#include "Hashing.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"
#include "StringView.h"
#include "Time.h"
#include "util/HexUtils.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"

#include <esp_ota_ops.h>

#include <LittleFS.h>
#include <WiFi.h>

#include <sstream>

#define OPENSHOCK_FW_CDN_CHANNEL_URL(ch) OPENSHOCK_FW_CDN_URL("/version-" ch ".txt")

#define OPENSHOCK_FW_CDN_STABLE_URL  OPENSHOCK_FW_CDN_CHANNEL_URL("stable")
#define OPENSHOCK_FW_CDN_BETA_URL    OPENSHOCK_FW_CDN_CHANNEL_URL("beta")
#define OPENSHOCK_FW_CDN_DEVELOP_URL OPENSHOCK_FW_CDN_CHANNEL_URL("develop")

#define OPENSHOCK_FW_CDN_BOARDS_BASE_URL_FORMAT  OPENSHOCK_FW_CDN_URL("/%s")
#define OPENSHOCK_FW_CDN_BOARDS_INDEX_URL_FORMAT OPENSHOCK_FW_CDN_BOARDS_BASE_URL_FORMAT "/boards.txt"

#define OPENSHOCK_FW_CDN_VERSION_BASE_URL_FORMAT OPENSHOCK_FW_CDN_BOARDS_BASE_URL_FORMAT "/" OPENSHOCK_FW_BOARD

#define OPENSHOCK_FW_CDN_APP_URL_FORMAT           OPENSHOCK_FW_CDN_VERSION_BASE_URL_FORMAT "/app.bin"
#define OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT    OPENSHOCK_FW_CDN_VERSION_BASE_URL_FORMAT "/staticfs.bin"
#define OPENSHOCK_FW_CDN_SHA256_HASHES_URL_FORMAT OPENSHOCK_FW_CDN_VERSION_BASE_URL_FORMAT "/hashes.sha256.txt"

const char* const TAG = "OtaUpdateManager";

/// @brief Stops initArduino() from handling OTA rollbacks
/// @todo Get rid of Arduino entirely. >:(
///
/// @see .platformio/packages/framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c
/// @return true
bool verifyRollbackLater() {
  return true;
}

using namespace OpenShock;

enum OtaTaskEventFlag : std::uint32_t {
  OTA_TASK_EVENT_UPDATE_REQUESTED  = 1 << 0,
  OTA_TASK_EVENT_WIFI_DISCONNECTED = 1 << 1,  // If both connected and disconnected are set, disconnected takes priority.
  OTA_TASK_EVENT_WIFI_CONNECTED    = 1 << 2,
};

static bool _otaValidatingApp = false;
static TaskHandle_t _taskHandle;
static std::string _requestedVersion;
static SemaphoreHandle_t _requestedVersionMutex = xSemaphoreCreateMutex();

bool _tryQueueUpdateRequest(StringView version) {
  if (xSemaphoreTake(_requestedVersionMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  if (!_requestedVersion.empty()) {
    ESP_LOGW(TAG, "Update request already queued");
    xSemaphoreGive(_requestedVersionMutex);
    return false;
  }

  _requestedVersion = version.toString();

  xSemaphoreGive(_requestedVersionMutex);

  xTaskNotify(_taskHandle, OTA_TASK_EVENT_UPDATE_REQUESTED, eSetBits);

  return true;
}

bool _tryGetRequestedVersion(std::string& version) {
  if (xSemaphoreTake(_requestedVersionMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  if (_requestedVersion.empty()) {
    xSemaphoreGive(_requestedVersionMutex);
    return false;
  }

  version = std::move(_requestedVersion);
  _requestedVersion.clear();

  xSemaphoreGive(_requestedVersionMutex);

  return true;
}

bool _printfToString(std::string& out, const char* format, ...) {
  constexpr std::size_t STACK_BUFFER_SIZE = 128;

  char buffer[STACK_BUFFER_SIZE];
  char* bufferPtr = buffer;

  va_list args;

  // Try format with stack buffer.
  va_start(args, format);
  int result = vsnprintf(buffer, STACK_BUFFER_SIZE, format, args);
  va_end(args);

  // If result is negative, something went wrong.
  if (result < 0) {
    ESP_LOGE(TAG, "Failed to format string");
    return false;
  }

  if (result >= STACK_BUFFER_SIZE) {
    // Account for null terminator.
    result += 1;

    // Allocate heap buffer.
    bufferPtr = new char[result];

    // Try format with heap buffer.
    va_start(args, format);
    result = vsnprintf(bufferPtr, result, format, args);
    va_end(args);

    // If we still fail, something is wrong.
    // Free heap buffer and return false.
    if (result < 0) {
      delete[] bufferPtr;
      ESP_LOGE(TAG, "Failed to format string");
      return false;
    }
  }

  // Set output string.
  out = std::string(bufferPtr, result);

  // Free heap buffer if we used it.
  if (bufferPtr != buffer) {
    delete[] bufferPtr;
  }

  return true;
}

void _otaEvGotIPHandler(arduino_event_t* event) {
  (void)event;
  xTaskNotify(_taskHandle, OTA_TASK_EVENT_WIFI_CONNECTED, eSetBits);
}
void _otaEvWiFiDisconnectedHandler(arduino_event_t* event) {
  (void)event;
  xTaskNotify(_taskHandle, OTA_TASK_EVENT_WIFI_DISCONNECTED, eSetBits);
}

bool _tryForceCloseCaptivePortal(TickType_t timeout) {
  OpenShock::CaptivePortal::SetForceClosed(true);
  while (OpenShock::CaptivePortal::IsRunning() && timeout > 0) {
    vTaskDelay(pdMS_TO_TICKS(std::min(timeout, 100U)));
    OpenShock::CaptivePortal::SetForceClosed(true);
  }

  return !OpenShock::CaptivePortal::IsRunning();
}

bool _flashPartition(const esp_partition_t* partition, StringView remoteUrl, const std::uint8_t (&remoteHash)[32], std::function<bool(std::size_t, std::size_t, float)> progressCallback = nullptr) {
  OpenShock::SHA256 sha256;
  if (!sha256.begin()) {
    ESP_LOGE(TAG, "Failed to initialize SHA256 hash");
    return false;
  }

  std::size_t contentLength  = 0;
  std::size_t contentWritten = 0;

  auto sizeValidator = [partition, &contentLength, progressCallback](std::size_t size) -> bool {
    if (size > partition->size) {
      ESP_LOGE(TAG, "Remote partition binary is too large");
      return false;
    }

    // Erase app partition.
    if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to erase partition in preparation for update");
      return false;
    }

    contentLength = size;
    progressCallback(0, contentLength, 0.0f);

    return true;
  };
  auto dataWriter = [partition, &sha256, &contentLength, &contentWritten, progressCallback](std::size_t offset, const std::uint8_t* data, std::size_t length) -> bool {
    if (esp_partition_write(partition, offset, data, length) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to write to partition");
      return false;
    }

    if (!sha256.update(data, length)) {
      ESP_LOGE(TAG, "Failed to update SHA256 hash");
      return false;
    }

    contentWritten += length;
    progressCallback(contentWritten, contentLength, static_cast<float>(contentWritten) / static_cast<float>(contentLength));

    return true;
  };

  // Start streaming binary to app partition.
  auto appBinaryResponse = OpenShock::HTTP::Download(
    remoteUrl,
    {
      {"Accept", "application/octet-stream"}
  },
    sizeValidator,
    dataWriter,
    {200, 304},
    180'000
  );  // 3 minutes
  if (appBinaryResponse.result != OpenShock::HTTP::RequestResult::Success) {
    ESP_LOGE(TAG, "Failed to download remote partition binary: [%u]", appBinaryResponse.code);
    return false;
  }

  progressCallback(contentLength, contentLength, 1.0f);
  ESP_LOGD(TAG, "Wrote %u bytes to partition", appBinaryResponse.data);

  std::array<std::uint8_t, 32> localHash;
  if (!sha256.finish(localHash)) {
    ESP_LOGE(TAG, "Failed to finish SHA256 hash");
    return false;
  }

  // Compare hashes.
  if (memcmp(localHash.data(), remoteHash, 32) != 0) {
    ESP_LOGE(TAG, "App binary hash mismatch");
    return false;
  }

  return true;
}

bool _flashAppPartition(const esp_partition_t* partition, StringView remoteUrl, const std::uint8_t (&remoteHash)[32]) {
  ESP_LOGD(TAG, "Flashing app partition");

  auto onProgress = [](std::size_t current, std::size_t total, float progress) -> bool {
    // TODO: Implement
    ESP_LOGD(TAG, "Flashing app partition: %u / %u (%.2f%%)", current, total, progress * 100.0f);
    return true;
  };

  if (!_flashPartition(partition, remoteUrl, remoteHash, onProgress)) {
    ESP_LOGE(TAG, "Failed to flash app partition");
    return false;
  }

  // Set app partition bootable.
  if (esp_ota_set_boot_partition(partition) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set app partition bootable");
    return false;
  }

  return true;
}

bool _flashFilesystemPartition(const esp_partition_t* parition, StringView remoteUrl, const std::uint8_t (&remoteHash)[32]) {
  // Make sure captive portal is stopped.
  if (!_tryForceCloseCaptivePortal(5000U)) {  // 5 seconds
    ESP_LOGE(TAG, "Failed to force close captive portal (timed out)");
    return false;
  }

  ESP_LOGD(TAG, "Flashing filesystem partition");

  auto onProgress = [](std::size_t current, std::size_t total, float progress) -> bool {
    // TODO: Implement
    ESP_LOGD(TAG, "Flashing filesystem partition: %u / %u (%.2f%%)", current, total, progress * 100.0f);
    return true;
  };

  if (!_flashPartition(parition, remoteUrl, remoteHash, onProgress)) {
    ESP_LOGE(TAG, "Failed to flash filesystem partition");
    return false;
  }

  // Attempt to mount filesystem.
  fs::LittleFSFS test;
  if (!test.begin(false, "/static", 10, "static0")) {
    ESP_LOGE(TAG, "Failed to mount filesystem");
    return false;
  }
  test.end();

  OpenShock::CaptivePortal::SetForceClosed(false);

  return true;
}

void _otaUpdateTask(void* arg) {
  (void)arg;

  ESP_LOGD(TAG, "OTA update task started");

  bool connected               = false;
  bool updateRequested         = false;
  std::int64_t lastUpdateCheck = 0;

  // Update task loop.
  while (true) {
    // Wait for event.
    uint32_t eventBits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &eventBits, pdMS_TO_TICKS(5000));  // TODO: wait for rest time

    updateRequested |= (eventBits & OTA_TASK_EVENT_UPDATE_REQUESTED) != 0;

    if ((eventBits & OTA_TASK_EVENT_WIFI_DISCONNECTED) != 0) {
      ESP_LOGD(TAG, "WiFi disconnected");
      connected = false;
      continue;  // No further processing needed.
    }

    if ((eventBits & OTA_TASK_EVENT_WIFI_CONNECTED) != 0 && !connected) {
      ESP_LOGD(TAG, "WiFi connected");
      connected = true;
    }

    // If we're not connected, continue.
    if (!connected) {
      ESP_LOGD(TAG, "Not connected, skipping update check");
      continue;
    }

    std::int64_t now = OpenShock::millis();

    Config::OtaUpdateConfig config;
    if (!Config::GetOtaUpdateConfig(config)) {
      ESP_LOGE(TAG, "Failed to get OTA update config");
      continue;
    }

    if (!config.isEnabled) {
      ESP_LOGD(TAG, "OTA updates are disabled, skipping update check");
      continue;
    }

    bool firstCheck       = lastUpdateCheck == 0;
    std::int64_t diff     = now - lastUpdateCheck;
    std::int64_t diffMins = diff / 60'000LL;

    bool check = false;
    check |= config.checkOnStartup && firstCheck;                           // On startup
    check |= config.checkPeriodically && diffMins >= config.checkInterval;  // Periodically
    check |= updateRequested && (firstCheck || diffMins >= 1);              // Update requested

    if (!check) {
      ESP_LOGD(TAG, "Skipping update check:");
      ESP_LOGD(TAG, "  Diff:         %f secs", static_cast<float>(diff) / 1'000.0f);
      ESP_LOGD(TAG, "  Check:        %s", check ? "true" : "false");
      ESP_LOGD(TAG, "  Startup:      %s", config.checkOnStartup ? "true" : "false");
      ESP_LOGD(TAG, "  Periodically: %s (%u minutes)", config.checkPeriodically ? "true" : "false", config.checkInterval);
      ESP_LOGD(TAG, "  Requested:    %s", updateRequested ? "true" : "false");
      continue;
    }

    lastUpdateCheck = now;

    if (config.requireManualApproval) {
      ESP_LOGD(TAG, "Manual approval required, skipping update check");
      // TODO: IMPLEMENT
      continue;
    }

    std::string version;
    if (updateRequested) {
      updateRequested = false;

      if (!_tryGetRequestedVersion(version)) {
        ESP_LOGE(TAG, "Failed to get requested version");
        continue;
      }

      ESP_LOGD(TAG, "Update requested for version %s", version.c_str());
    } else {
      ESP_LOGD(TAG, "Checking for updates");

      // Fetch current version.
      std::string version;
      if (!OtaUpdateManager::TryGetFirmwareVersion(config.updateChannel, version)) {
        ESP_LOGE(TAG, "Failed to fetch firmware version");
        continue;
      }

      ESP_LOGD(TAG, "Remote version: %s", version.c_str());
    }

    if (version == OPENSHOCK_FW_VERSION) {
      ESP_LOGI(TAG, "Requested version is already installed");  // TODO: Send error message to server
      continue;
    }

    // Fetch current release.
    OtaUpdateManager::FirmwareRelease release;
    if (!OtaUpdateManager::TryGetFirmwareRelease(version, release)) {
      ESP_LOGE(TAG, "Failed to fetch firmware release");  // TODO: Send error message to server
      continue;
    }

    // Print release.
    ESP_LOGD(TAG, "Firmware release:");
    ESP_LOGD(TAG, "  Version:                %s", version.c_str());
    ESP_LOGD(TAG, "  App binary URL:         %s", release.appBinaryUrl.c_str());
    ESP_LOGD(TAG, "  App binary hash:        %s", HexUtils::ToHex<32>(release.appBinaryHash).data());
    ESP_LOGD(TAG, "  Filesystem binary URL:  %s", release.filesystemBinaryUrl.c_str());
    ESP_LOGD(TAG, "  Filesystem binary hash: %s", HexUtils::ToHex<32>(release.filesystemBinaryHash).data());

    // Get available app update partition.
    const esp_partition_t* appPartition = esp_ota_get_next_update_partition(nullptr);
    if (appPartition == nullptr) {
      ESP_LOGE(TAG, "Failed to get app update partition");  // TODO: Send error message to server
      continue;
    }

    // Get filesystem partition.
    const esp_partition_t* filesystemPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "static0");
    if (filesystemPartition == nullptr) {
      ESP_LOGE(TAG, "Failed to find filesystem partition");  // TODO: Send error message to server
      continue;
    }

    // Flash app and filesystem partitions.
    if (!_flashFilesystemPartition(filesystemPartition, release.filesystemBinaryUrl, release.filesystemBinaryHash)) {
      continue;  // TODO: Send error message to server
    }
    if (!_flashAppPartition(appPartition, release.appBinaryUrl, release.appBinaryHash)) {
      continue;  // TODO: Send error message to server
    }

    // Restart.
    ESP_LOGI(TAG, "Restarting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
  }
}

bool _tryGetStringList(StringView url, std::vector<std::string>& list) {
  auto response = OpenShock::HTTP::GetString(
    url,
    {
      {"Accept", "text/plain"}
  },
    {200, 304}
  );
  if (response.result != OpenShock::HTTP::RequestResult::Success) {
    ESP_LOGE(TAG, "Failed to fetch list: [%u] %s", response.code, response.data.c_str());
    return false;
  }

  list.clear();

  OpenShock::StringView data = response.data;

  for (auto line : data.splitLines()) {
    line = line.trim();

    if (line.isNullOrEmpty()) {
      continue;
    }

    list.push_back(line.toString());
  }

  return true;
}

bool OtaUpdateManager::Init() {
  ESP_LOGD(TAG, "Fetching current partition");

  // Fetch current partition info.
  const esp_partition_t* partition = esp_ota_get_running_partition();
  if (partition == nullptr) {
    ESP_PANIC(TAG, "Failed to get currently running partition");
    return false;  // This will never be reached, but the compiler doesn't know that.
  }

  ESP_LOGD(TAG, "Fetching partition state");

  // Get OTA state for said partition.
  esp_ota_img_states_t states;
  esp_err_t err = esp_ota_get_state_partition(partition, &states);
  if (err != ESP_OK) {
    ESP_PANIC(TAG, "Failed to get partition state: %s", esp_err_to_name(err));
    return false;  // This will never be reached, but the compiler doesn't know that.
  }

  ESP_LOGD(TAG, "Partition state: %u", states);

  // If the currently booting partition is being verified, set correct state.
  _otaValidatingApp = states == ESP_OTA_IMG_PENDING_VERIFY;

  // Configure event triggers.
  Config::OtaUpdateConfig otaUpdateConfig;
  if (!Config::GetOtaUpdateConfig(otaUpdateConfig)) {
    ESP_LOGE(TAG, "Failed to get OTA update config");
    return false;
  }

  WiFi.onEvent(_otaEvGotIPHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_otaEvGotIPHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_otaEvWiFiDisconnectedHandler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // Start OTA update task.
  TaskUtils::TaskCreateExpensive(_otaUpdateTask, "OTA Update", 8192, nullptr, 1, &_taskHandle);

  return true;
}

bool OtaUpdateManager::TryGetFirmwareVersion(OtaUpdateChannel channel, std::string& version) {
  const char* channelIndexUrl = nullptr;
  switch (channel) {
    case OtaUpdateChannel::Stable:
      channelIndexUrl = OPENSHOCK_FW_CDN_STABLE_URL;
      break;
    case OtaUpdateChannel::Beta:
      channelIndexUrl = OPENSHOCK_FW_CDN_BETA_URL;
      break;
    case OtaUpdateChannel::Develop:
      channelIndexUrl = OPENSHOCK_FW_CDN_DEVELOP_URL;
      break;
    default:
      ESP_LOGE(TAG, "Unknown channel: %u", channel);
      return false;
  }

  ESP_LOGD(TAG, "Fetching firmware version from %s", channelIndexUrl);

  auto response = OpenShock::HTTP::GetString(
    channelIndexUrl,
    {
      {"Accept", "text/plain"}
  },
    {200, 304}
  );
  if (response.result != OpenShock::HTTP::RequestResult::Success) {
    ESP_LOGE(TAG, "Failed to fetch firmware version: [%u] %s", response.code, response.data.c_str());
    return false;
  }

  version = response.data;

  return true;
}

bool OtaUpdateManager::TryGetFirmwareBoards(const std::string& version, std::vector<std::string>& boards) {
  std::string channelIndexUrl;
  if (!_printfToString(channelIndexUrl, OPENSHOCK_FW_CDN_BOARDS_INDEX_URL_FORMAT, version.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }

  ESP_LOGD(TAG, "Fetching firmware boards from %s", channelIndexUrl.c_str());

  if (!_tryGetStringList(channelIndexUrl.c_str(), boards)) {
    ESP_LOGE(TAG, "Failed to fetch firmware boards");
    return false;
  }

  return true;
}

bool _tryParseIntoHash(const std::string& hash, std::uint8_t (&hashBytes)[32]) {
  if (!HexUtils::TryParseHex(hash.data(), hash.size(), hashBytes, 32)) {
    ESP_LOGE(TAG, "Failed to parse hash: %.*s", hash.size(), hash.data());
    return false;
  }

  return true;
}

bool OtaUpdateManager::TryGetFirmwareRelease(const std::string& version, FirmwareRelease& release) {
  if (!_printfToString(release.appBinaryUrl, OPENSHOCK_FW_CDN_APP_URL_FORMAT, version.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }

  if (!_printfToString(release.filesystemBinaryUrl, OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT, version.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }

  // Construct hash URLs.
  std::string sha256HashesUrl;
  if (!_printfToString(sha256HashesUrl, OPENSHOCK_FW_CDN_SHA256_HASHES_URL_FORMAT, version.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }

  // Fetch hashes.
  auto sha256HashesResponse = OpenShock::HTTP::GetString(
    sha256HashesUrl.c_str(),
    {
      {"Accept", "text/plain"}
  },
    {200, 304}
  );
  if (sha256HashesResponse.result != OpenShock::HTTP::RequestResult::Success) {
    ESP_LOGE(TAG, "Failed to fetch hashes: [%u] %s", sha256HashesResponse.code, sha256HashesResponse.data.c_str());
    return false;
  }

  auto hashesLines = OpenShock::StringView(sha256HashesResponse.data).splitLines();

  // Parse hashes.
  bool foundAppHash = false, foundFilesystemHash = false;
  for (auto line : hashesLines) {
    auto parts = line.splitWhitespace();
    if (parts.size() != 2) {
      ESP_LOGE(TAG, "Invalid hashes entry: %.*s", line.size(), line.data());
      return false;
    }

    auto hash = parts[0].trim();
    auto file = parts[1].trim();

    if (file.startsWith("./")) {
      file = file.substr(2);
    }

    if (hash.size() != 64) {
      ESP_LOGE(TAG, "Invalid hash: %.*s", hash.size(), hash.data());
      return false;
    }

    if (file == "app.bin") {
      if (foundAppHash) {
        ESP_LOGE(TAG, "Duplicate hash for app.bin");
        return false;
      }

      if (!_tryParseIntoHash(hash.toString(), release.appBinaryHash)) {
        return false;
      }

      foundAppHash = true;
    } else if (file == "staticfs.bin") {
      if (foundFilesystemHash) {
        ESP_LOGE(TAG, "Duplicate hash for staticfs.bin");
        return false;
      }

      if (!_tryParseIntoHash(hash.toString(), release.filesystemBinaryHash)) {
        return false;
      }

      foundFilesystemHash = true;
    }
  }

  return true;
}

bool OtaUpdateManager::TryStartFirmwareInstallation(OpenShock::StringView version) {
  ESP_LOGD(TAG, "Requesting firmware version %.*s", version.size(), version.data());

  return _tryQueueUpdateRequest(version);
}

bool OtaUpdateManager::IsValidatingApp() {
  return _otaValidatingApp;
}

void OtaUpdateManager::InvalidateAndRollback() {
  switch (esp_ota_mark_app_invalid_rollback_and_reboot()) {
    case ESP_FAIL:
      ESP_PANIC(TAG, "Rollback failed");
    case ESP_ERR_OTA_ROLLBACK_FAILED:
      ESP_PANIC(TAG, "Rollback failed");
    default:
      ESP_PANIC(TAG, "Rollback failed");
  }
}

void OtaUpdateManager::ValidateApp() {
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    ESP_PANIC(TAG, "Unable to mark app as valid, WTF?");  // TODO: Wtf do we do here?
  }
}
