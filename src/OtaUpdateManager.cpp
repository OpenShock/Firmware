#include "OtaUpdateManager.h"

#include "config/Config.h"
#include "Constants.h"
#include "GatewayConnectionManager.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"
#include "Time.h"
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

#define OPENSHOCK_FW_CDN_APP_URL_FORMAT      OPENSHOCK_FW_CDN_VERSION_BASE_URL_FORMAT "/app.bin"
#define OPENSHOCK_FW_CDN_APP_HASH_URL_FORMAT OPENSHOCK_FW_CDN_APP_URL_FORMAT ".sha256"

#define OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT      OPENSHOCK_FW_CDN_VERSION_BASE_URL_FORMAT "/staticfs.bin"
#define OPENSHOCK_FW_CDN_FILESYSTEM_HASH_URL_FORMAT OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT ".sha256"

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

void _otaEvGotIPHandler(arduino_event_t* event) {
  (void)event;
  xTaskNotify(_taskHandle, OTA_TASK_EVENT_WIFI_CONNECTED, eSetBits);
}
void _otaEvWiFiDisconnectedHandler(arduino_event_t* event) {
  (void)event;
  xTaskNotify(_taskHandle, OTA_TASK_EVENT_WIFI_DISCONNECTED, eSetBits);
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
    xTaskNotifyWait(0, UINT32_MAX, &eventBits, pdMS_TO_TICKS(5000));

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
      continue;
    }

    std::int64_t now = OpenShock::millis();

    bool check = false;
    if (lastUpdateCheck != 0) {
      std::int64_t diff = now - lastUpdateCheck;

      check = diff >= 1'800'000LL;                // 30 minutes

      if (updateRequested && diff >= 60'000LL) {  // 1 minute
        check = true;
      }
    } else {
      check = true;
    }

    lastUpdateCheck = now;

    if (!check) {
      continue;
    }

    updateRequested = false;

    ESP_LOGD(TAG, "Checking for updates");

    // Fetch current version.
    std::vector<std::string> versions;
    if (!OtaUpdateManager::TryGetFirmwareVersions(OtaUpdateChannel::Beta, versions)) {
      ESP_LOGE(TAG, "Failed to fetch firmware versions");
      continue;
    }

    // Print versions.
    ESP_LOGD(TAG, "Firmware versions:");
    for (const auto& version : versions) {
      ESP_LOGD(TAG, "  %s", version.c_str());
    }

    // Fetch current release.
    OtaUpdateManager::FirmwareRelease release;
    if (!OtaUpdateManager::TryGetFirmwareRelease(versions[0], release)) {
      ESP_LOGE(TAG, "Failed to fetch firmware release");
      continue;
    }

    // Print release.
    ESP_LOGD(TAG, "Firmware release:");
    ESP_LOGD(TAG, "  Version:                %s", release.version.c_str());
    ESP_LOGD(TAG, "  App binary URL:         %s", release.appBinaryUrl.c_str());
    ESP_LOGD(TAG, "  App binary hash:        %s", release.appBinaryHash.c_str());
    ESP_LOGD(TAG, "  Filesystem binary URL:  %s", release.filesystemBinaryUrl.c_str());
    ESP_LOGD(TAG, "  Filesystem binary hash: %s", release.filesystemBinaryHash.c_str());
  }
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

bool _tryGetStringList(const char* url, std::vector<std::string>& list) {
  auto response = OpenShock::HTTP::GetString(url, {{ "Accept", "text/plain" }}, { 200, 304 });
  if (response.result != OpenShock::HTTP::RequestResult::Success) {
    ESP_LOGE(TAG, "Failed to fetch list: [%u] %s", response.code, response.data.c_str());
    return false;
  }

  list.clear();

  std::stringstream data(response.data);

  // Split response into lines.
  std::string line;
  while (std::getline(data, line)) {
    // Skip empty lines.
    if (line.empty() || std::all_of(line.begin(), line.end(), isspace)) {
      continue;
    }

    list.push_back(line);
  }

  return true;
}

// Retries if buffer is too small.
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

bool OtaUpdateManager::TryGetFirmwareVersions(OtaUpdateChannel channel, std::vector<std::string>& versions) {
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

  ESP_LOGD(TAG, "Fetching firmware versions from %s", channelIndexUrl);

  if (!_tryGetStringList(channelIndexUrl, versions)) {
    ESP_LOGE(TAG, "Failed to fetch firmware versions");
    return false;
  }

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

bool OtaUpdateManager::TryGetFirmwareRelease(const std::string& version, FirmwareRelease& release) {
  release.version = version;

  if (!_printfToString(release.appBinaryUrl, OPENSHOCK_FW_CDN_APP_URL_FORMAT, version.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }

  if (!_printfToString(release.filesystemBinaryUrl, OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT, version.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }

  // Construct hash URLs.
  std::string appBinaryHashUrl;
  if (!_printfToString(appBinaryHashUrl, OPENSHOCK_FW_CDN_APP_HASH_URL_FORMAT, version.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }
  std::string filesystemBinaryHashUrl;
  if (!_printfToString(filesystemBinaryHashUrl, OPENSHOCK_FW_CDN_FILESYSTEM_HASH_URL_FORMAT, version.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }

  // Fetch hashes.
  auto appBinaryHashResponse = OpenShock::HTTP::GetString(appBinaryHashUrl.c_str(), {{ "Accept", "text/plain" }}, { 200, 304 });
  if (appBinaryHashResponse.result != OpenShock::HTTP::RequestResult::Success) {
    ESP_LOGE(TAG, "Failed to fetch app binary hash: [%u] %s", appBinaryHashResponse.code, appBinaryHashResponse.data.c_str());
    return false;
  }

  auto filesystemBinaryHashResponse = OpenShock::HTTP::GetString(filesystemBinaryHashUrl.c_str(), {{ "Accept", "text/plain" }}, { 200, 304 });
  if (filesystemBinaryHashResponse.result != OpenShock::HTTP::RequestResult::Success) {
    ESP_LOGE(TAG, "Failed to fetch filesystem binary hash: [%u] %s", filesystemBinaryHashResponse.code, filesystemBinaryHashResponse.data.c_str());
    return false;
  }

  release.appBinaryHash        = appBinaryHashResponse.data.c_str();
  release.filesystemBinaryHash = filesystemBinaryHashResponse.data.c_str();

  return true;
}

bool OtaUpdateManager::IsValidatingApp() {
  return _otaValidatingApp;
}

void OtaUpdateManager::InvalidateAndRollback() {
  esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();

  // If we get here, something went VERY wrong.
  // TODO: Wtf do we do here?

  // I have no idea, placeholder:

  vTaskDelay(pdMS_TO_TICKS(5000));

  esp_restart();
}

void OtaUpdateManager::ValidateApp() {
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    ESP_PANIC(TAG, "Unable to mark app as valid, WTF?");  // TODO: Wtf do we do here?
  }
}
