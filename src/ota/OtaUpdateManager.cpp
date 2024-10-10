#include <freertos/FreeRTOS.h>

#include "ota/OtaUpdateManager.h"

const char* const TAG = "OtaUpdateManager";

#include "Common.h"
#include "config/Config.h"
#include "http/FirmwareCDN.h"
#include "Logging.h"
#include "ota/OtaUpdateClient.h"
#include "ota/OtaUpdateStep.h"
#include "SemVer.h"
#include "util/StringUtils.h"
#include "util/TaskUtils.h"

#include <WiFi.h>  // TODO: Get rid of Arduino entirely. >:(

#include <esp_ota_ops.h>
#include <freertos/task.h>

#include <cstdint>
#include <cstring>
#include <memory>

using namespace std::string_view_literals;

/// @brief Stops initArduino() from handling OTA rollbacks
/// @todo Get rid of Arduino entirely. >:(
///
/// @see .platformio/packages/framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c
/// @return true
bool verifyRollbackLater()
{
  return true;
}

using namespace OpenShock;

enum OtaTaskEventFlag : uint32_t {
  OTA_TASK_EVENT_UPDATE_REQUESTED  = 1 << 0,
  OTA_TASK_EVENT_WIFI_DISCONNECTED = 1 << 1,  // If both connected and disconnected are set, disconnected takes priority.
  OTA_TASK_EVENT_WIFI_CONNECTED    = 1 << 2,
};

static esp_ota_img_states_t _otaImageState;
static OpenShock::FirmwareBootType _bootType;
static TaskHandle_t _watcherTaskHandle                = nullptr;
static SemaphoreHandle_t _updateClientMutex           = xSemaphoreCreateMutex();
static std::unique_ptr<OtaUpdateClient> _updateClient = nullptr;

bool _tryStartUpdate(const OpenShock::SemVer& version)
{
  if (xSemaphoreTake(_updateClientMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    OS_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  if (_updateClient != nullptr) {
    xSemaphoreGive(_updateClientMutex);
    OS_LOGE(TAG, "Update client already started");
    return false;
  }

  _updateClient = std::make_unique<OtaUpdateClient>(version);

  if (!_updateClient->Start()) {
    _updateClient.reset();
    xSemaphoreGive(_updateClientMutex);
    OS_LOGE(TAG, "Failed to start update client");
    return false;
  }

  xSemaphoreGive(_updateClientMutex);

  OS_LOGD(TAG, "Update client started");

  return true;
}

void _otaEvGotIPHandler(arduino_event_t*)
{
  xTaskNotify(_watcherTaskHandle, OTA_TASK_EVENT_WIFI_CONNECTED, eSetBits);
}
void _otaEvWiFiDisconnectedHandler(arduino_event_t*)
{
  xTaskNotify(_watcherTaskHandle, OTA_TASK_EVENT_WIFI_DISCONNECTED, eSetBits);
}

void _otaWatcherTask(void*)
{
  OS_LOGD(TAG, "OTA update task started");

  bool connected          = false;
  bool updateRequested    = false;
  int64_t lastUpdateCheck = 0;

  // Update task loop.
  while (true) {
    // Wait for event.
    uint32_t eventBits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &eventBits, pdMS_TO_TICKS(5000));  // TODO: wait for rest time

    updateRequested |= (eventBits & OTA_TASK_EVENT_UPDATE_REQUESTED) != 0;

    if ((eventBits & OTA_TASK_EVENT_WIFI_DISCONNECTED) != 0) {
      OS_LOGD(TAG, "WiFi disconnected");
      connected = false;
      continue;  // No further processing needed.
    }

    if ((eventBits & OTA_TASK_EVENT_WIFI_CONNECTED) != 0 && !connected) {
      OS_LOGD(TAG, "WiFi connected");
      connected = true;
    }

    // If we're not connected, continue.
    if (!connected) {
      continue;
    }

    int64_t now = OpenShock::millis();

    Config::OtaUpdateConfig config;
    if (!Config::GetOtaUpdateConfig(config)) {
      OS_LOGE(TAG, "Failed to get OTA update config");
      continue;
    }

    if (!config.isEnabled) {
      OS_LOGD(TAG, "OTA updates are disabled, skipping update check");
      continue;
    }

    bool firstCheck  = lastUpdateCheck == 0;
    int64_t diff     = now - lastUpdateCheck;
    int64_t diffMins = diff / 60'000LL;

    bool check = false;
    check |= config.checkOnStartup && firstCheck;                           // On startup
    check |= config.checkPeriodically && diffMins >= config.checkInterval;  // Periodically
    check |= updateRequested && (firstCheck || diffMins >= 1);              // Update requested

    if (!check) {
      continue;
    }

    lastUpdateCheck = now;

    if (config.requireManualApproval) {
      OS_LOGD(TAG, "Manual approval required, skipping update check");
      // TODO: IMPLEMENT
      continue;
    }

    OpenShock::SemVer version;
    if (updateRequested) {
      updateRequested = false;

      if (!_tryGetRequestedVersion(version)) {
        OS_LOGE(TAG, "Failed to get requested version");
        continue;
      }

      OS_LOGD(TAG, "Update requested for version %s", version.toString().c_str());  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this
    } else {
      OS_LOGD(TAG, "Checking for updates");

      // Fetch current version.
      if (!OtaUpdateManager::TryGetFirmwareVersion(config.updateChannel, version)) {
        OS_LOGE(TAG, "Failed to fetch firmware version");
        continue;
      }

      OS_LOGD(TAG, "Remote version: %s", version.toString().c_str());  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this
    }

    if (version.toString() == OPENSHOCK_FW_VERSION) {  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this
      OS_LOGI(TAG, "Requested version is already installed");
      continue;
    }
  }
}

bool OtaUpdateManager::Init()
{
  OS_LOGN(TAG, "Fetching current partition");

  // Fetch current partition info.
  const esp_partition_t* partition = esp_ota_get_running_partition();
  if (partition == nullptr) {
    OS_PANIC(TAG, "Failed to get currently running partition");
    return false;  // This will never be reached, but the compiler doesn't know that.
  }

  OS_LOGD(TAG, "Fetching partition state");

  // Get OTA state for said partition.
  esp_err_t err = esp_ota_get_state_partition(partition, &_otaImageState);
  if (err != ESP_OK) {
    OS_PANIC(TAG, "Failed to get partition state: %s", esp_err_to_name(err));
    return false;  // This will never be reached, but the compiler doesn't know that.
  }

  OS_LOGD(TAG, "Fetching previous update step");
  OtaUpdateStep updateStep;
  if (!Config::GetOtaUpdateStep(updateStep)) {
    OS_LOGE(TAG, "Failed to get OTA update step");
    return false;
  }

  // Infer boot type from update step.
  switch (updateStep) {
    case OtaUpdateStep::Updated:
      _bootType = FirmwareBootType::NewFirmware;
      break;
    case OtaUpdateStep::Validating:  // If the update step is validating, we have failed in the middle of validating the new firmware, meaning this is a rollback.
    case OtaUpdateStep::RollingBack:
      _bootType = FirmwareBootType::Rollback;
      break;
    default:
      _bootType = FirmwareBootType::Normal;
      break;
  }

  if (updateStep == OtaUpdateStep::Updated) {
    if (!Config::SetOtaUpdateStep(OtaUpdateStep::Validating)) {
      OS_PANIC(TAG, "Failed to set OTA update step in critical section");  // TODO: THIS IS A CRITICAL SECTION, WHAT DO WE DO?
    }
  }

  WiFi.onEvent(_otaEvGotIPHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_otaEvGotIPHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_otaEvWiFiDisconnectedHandler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  if (TaskUtils::TaskCreateExpensive(_otaWatcherTask, "OtaWatcherTask", 8192, nullptr, 1, &_watcherTaskHandle) != pdPASS) {
    OS_LOGE(TAG, "Failed to create OTA watcher task");
    return false;
  }

  return true;
}

bool OtaUpdateManager::TryGetFirmwareRelease(const OpenShock::SemVer& version, FirmwareReleaseInfo& release)
{
  auto versionStr = version.toString();  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

  if (!FormatToString(release.appBinaryUrl, OPENSHOCK_FW_CDN_APP_URL_FORMAT, versionStr.c_str())) {
    OS_LOGE(TAG, "Failed to format URL");
    return false;
  }

  if (!FormatToString(release.filesystemBinaryUrl, OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT, versionStr.c_str())) {
    OS_LOGE(TAG, "Failed to format URL");
    return false;
  }

  // Fetch hashes.
  auto response = HTTP::FirmwareCDN::GetFirmwareBinaryHashes(version);
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to fetch hashes: [%u]", response.code);
    return false;
  }

  for (auto binaryHash : response.data) {
    if (binaryHash.name == "app.bin") {
      static_assert(sizeof(release.appBinaryHash) == sizeof(binaryHash.hash), "Hash size mismatch");
      memcpy(release.appBinaryHash, binaryHash.hash, sizeof(release.appBinaryHash));
    } else if (binaryHash.name == "staticfs.bin") {
      static_assert(sizeof(release.filesystemBinaryHash) == sizeof(binaryHash.hash), "Hash size mismatch");
      memcpy(release.filesystemBinaryHash, binaryHash.hash, sizeof(release.filesystemBinaryHash));
    }
  }

  return true;
}

bool OtaUpdateManager::TryStartFirmwareInstallation(const OpenShock::SemVer& version)
{
  OS_LOGD(TAG, "Requesting firmware version %s", version.toString().c_str());  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

  return _tryStartUpdate(version);
}

FirmwareBootType OtaUpdateManager::GetFirmwareBootType()
{
  return _bootType;
}

bool OtaUpdateManager::IsValidatingApp()
{
  return _otaImageState == ESP_OTA_IMG_PENDING_VERIFY;
}

void OtaUpdateManager::InvalidateAndRollback()
{
  // Set OTA boot type in config.
  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::RollingBack)) {
    OS_PANIC(TAG, "Failed to set OTA firmware boot type in critical section");  // TODO: THIS IS A CRITICAL SECTION, WHAT DO WE DO?
    return;
  }

  switch (esp_ota_mark_app_invalid_rollback_and_reboot()) {
    case ESP_FAIL:
      OS_LOGE(TAG, "Rollback failed (ESP_FAIL)");
      break;
    case ESP_ERR_OTA_ROLLBACK_FAILED:
      OS_LOGE(TAG, "Rollback failed (ESP_ERR_OTA_ROLLBACK_FAILED)");
      break;
    default:
      OS_LOGE(TAG, "Rollback failed (Unknown)");
      break;
  }

  // Set OTA boot type in config.
  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::None)) {
    OS_LOGE(TAG, "Failed to set OTA firmware boot type");
  }

  esp_restart();
}

void OtaUpdateManager::ValidateApp()
{
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    OS_PANIC(TAG, "Unable to mark app as valid, WTF?");  // TODO: Wtf do we do here?
  }

  // Set OTA boot type in config.
  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::Validated)) {
    OS_PANIC(TAG, "Failed to set OTA firmware boot type in critical section");  // TODO: THIS IS A CRITICAL SECTION, WHAT DO WE DO?
  }

  _otaImageState = ESP_OTA_IMG_VALID;
}
