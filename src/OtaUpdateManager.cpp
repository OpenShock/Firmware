#include "OtaUpdateManager.h"

#include "config/Config.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "Constants.h"
#include "wifi/WiFiManager.h"
#include "http/HTTPRequestManager.h"

#include <esp_ota_ops.h>

#include <LittleFS.h>
#include <WiFi.h>

#include <sstream>

#define OPENSHOCK_FW_CDN_STABLE_URL OPENSHOCK_FW_CDN_URL("/versions-stable.txt")
#define OPENSHOCK_FW_CDN_BETA_URL OPENSHOCK_FW_CDN_URL("/versions-beta.txt")
#define OPENSHOCK_FW_CDN_DEV_URL OPENSHOCK_FW_CDN_URL("/versions-develop.txt")

#define OPENSHOCK_FW_CDN_BOARDS_BASE_URL_FORMAT OPENSHOCK_FW_CDN_URL("/%s")
#define OPENSHOCK_FW_CDN_BOARDS_INDEX_URL_FORMAT OPENSHOCK_FW_CDN_BOARDS_BASE_URL_FORMAT "/boards.txt"

#define OPENSHOCK_FW_CDN_VERSION_BASE_URL_FORMAT OPENSHOCK_FW_CDN_BOARDS_BASE_URL_FORMAT "/%s"

#define OPENSHOCK_FW_CDN_APP_URL_FORMAT OPENSHOCK_FW_CDN_VERSION_BASE_URL_FORMAT "/app.bin"
#define OPENSHOCK_FW_CDN_APP_HASH_URL_FORMAT OPENSHOCK_FW_CDN_APP_URL_FORMAT ".sha256"

#define OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT OPENSHOCK_FW_CDN_VERSION_BASE_URL_FORMAT "/staticfs.bin"
#define OPENSHOCK_FW_CDN_FILESYSTEM_HASH_URL_FORMAT OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT ".sha256"

/// @brief Stops initArduino() from handling OTA rollbacks
/// @todo Get rid of Arduino entirely. >:(
///
/// @see .platformio/packages/framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c
/// @return true
bool verifyRollbackLater() {
  return true;
}

using namespace OpenShock;

const char* TAG = "OtaUpdateManager";

static bool _otaValidatingApp = false;

void OtaUpdateManager::Init() {
  ESP_LOGD(TAG, "Fetching current partition");

  // Fetch current partition info.
  const esp_partition_t* partition = esp_ota_get_running_partition();

  ESP_LOGD(TAG, "Fetching partition state");

  // Get OTA state for said partition.
  esp_ota_img_states_t states;
  esp_ota_get_state_partition(partition, &states);

  ESP_LOGD(TAG, "Partition state: %u", states);

  // If the currently booting partition is being verified, set correct state.
  _otaValidatingApp = states == ESP_OTA_IMG_PENDING_VERIFY;
}

bool _tryGetStringList(const char* url, std::vector<std::string>& list) {
  auto response = OpenShock::HTTP::GetString(url, {{ "Accept", "text/plain" }}, { 200, 304 });
  if (response.result != OpenShock::HTTP::RequestResult::Success) {
    ESP_LOGE(TAG, "Failed to fetch list: [%u] %s", response.code, response.data.c_str());
    return false;
  }

  list.clear();

  std::stringstream data;
  data << response.data.c_str();

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
  int result = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  // If we fail with less than STACK_BUFFER_SIZE, something is wrong.
  if (result < STACK_BUFFER_SIZE) {
    ESP_LOGE(TAG, "Failed to format string");
    return false;
  }

  if (result >= sizeof(buffer)) {
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

bool OtaUpdateManager::TryGetFirmwareVersions(FirmwareReleaseChannel channel, std::vector<std::string>& versions) {
  const char* channelIndexUrl = nullptr;
  switch (channel) {
    case FirmwareReleaseChannel::Stable:
      channelIndexUrl = OPENSHOCK_FW_CDN_STABLE_URL;
      break;
    case FirmwareReleaseChannel::Beta:
      channelIndexUrl = OPENSHOCK_FW_CDN_BETA_URL;
      break;
    case FirmwareReleaseChannel::Dev:
      channelIndexUrl = OPENSHOCK_FW_CDN_DEV_URL;
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

bool OtaUpdateManager::TryGetFirmwareRelease(const std::string& version, const std::string& board, FirmwareRelease& release) {
  release.board = board;
  release.version = version;

  if (!_printfToString(release.appBinaryUrl, OPENSHOCK_FW_CDN_APP_URL_FORMAT, version.c_str(), board.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }

  if (!_printfToString(release.filesystemBinaryUrl, OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT, version.c_str(), board.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }

  // Construct hash URLs.
  std::string appBinaryHashUrl;
  if (!_printfToString(appBinaryHashUrl, OPENSHOCK_FW_CDN_APP_HASH_URL_FORMAT, version.c_str(), board.c_str())) {
    ESP_LOGE(TAG, "Failed to format URL");
    return false;
  }
  std::string filesystemBinaryHashUrl;
  if (!_printfToString(filesystemBinaryHashUrl, OPENSHOCK_FW_CDN_FILESYSTEM_HASH_URL_FORMAT, version.c_str(), board.c_str())) {
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

  release.appBinaryHash = appBinaryHashResponse.data.c_str();
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
