#include <freertos/FreeRTOS.h>

#include "ota/OtaUpdateManager.h"

const char* const TAG = "OtaUpdateManager";

#include "Common.h"
#include "config/Config.h"
#include "Core.h"
#include "http/FirmwareCDN.h"
#include "Logging.h"
#include "ota/OtaUpdateClient.h"
#include "ota/OtaUpdateStep.h"
#include "SemVer.h"
#include "SimpleMutex.h"
#include "util/TaskUtils.h"

#include <esp_event.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
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

enum OtaTaskEventFlag : uint32_t {
  OTA_TASK_EVENT_UPDATE_REQUESTED  = 1 << 0,
  OTA_TASK_EVENT_WIFI_DISCONNECTED = 1 << 1,  // If both connected and disconnected are set, disconnected takes priority.
  OTA_TASK_EVENT_WIFI_CONNECTED    = 1 << 2,
};

static esp_ota_img_states_t s_otaImageState;
static OpenShock::FirmwareBootType s_bootType;
static TaskHandle_t s_taskHandle;
static OpenShock::SemVer s_requestedVersion;
static OpenShock::SimpleMutex s_requestedVersionMutex = {};

using namespace OpenShock;

static bool tryQueueUpdateRequest(const OpenShock::SemVer& version)
{
  if (!s_requestedVersionMutex.lock(pdMS_TO_TICKS(1000))) {
    OS_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  s_requestedVersion = version;

  s_requestedVersionMutex.unlock();

  xTaskNotify(s_taskHandle, OTA_TASK_EVENT_UPDATE_REQUESTED, eSetBits);

  return true;
}

static bool tryGetRequestedVersion(OpenShock::SemVer& version)
{
  if (!s_requestedVersionMutex.lock(pdMS_TO_TICKS(1000))) {
    OS_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  version = s_requestedVersion;

  s_requestedVersionMutex.unlock();

  return true;
}

static void evWiFiDisconnectedHandler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_id;
  (void)event_data;

  xTaskNotify(s_taskHandle, OTA_TASK_EVENT_WIFI_DISCONNECTED, eSetBits);
}

static void evIpEventHandler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_data;

  switch (event_id) {
    case IP_EVENT_GOT_IP6:
    case IP_EVENT_STA_GOT_IP:
      xTaskNotify(s_taskHandle, OTA_TASK_EVENT_WIFI_CONNECTED, eSetBits);
      break;
    case IP_EVENT_STA_LOST_IP:
      xTaskNotify(s_taskHandle, OTA_TASK_EVENT_WIFI_DISCONNECTED, eSetBits);
      break;
    default:
      return;
  }
}

static void otaUpdateTask(void* pvParameters)
{
  (void)pvParameters;

  bool connected          = false;
  bool updateRequested    = false;
  int64_t lastUpdateCheck = 0;

  for (;;) {
    // Wait for event
    uint32_t eventBits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &eventBits, pdMS_TO_TICKS(5000));

    updateRequested |= (eventBits & OTA_TASK_EVENT_UPDATE_REQUESTED) != 0;

    if ((eventBits & OTA_TASK_EVENT_WIFI_DISCONNECTED) != 0) {
      OS_LOGD(TAG, "WiFi disconnected");
      connected = false;
      continue;
    }

    if ((eventBits & OTA_TASK_EVENT_WIFI_CONNECTED) != 0 && !connected) {
      OS_LOGD(TAG, "WiFi connected");
      connected = true;
    }

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
    check |= config.checkOnStartup && firstCheck;
    check |= config.checkPeriodically && diffMins >= config.checkInterval;
    check |= updateRequested && (firstCheck || diffMins >= 1);

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

      if (!tryGetRequestedVersion(version)) {
        OS_LOGE(TAG, "Failed to get requested version");
        continue;
      }
    } else {
      OS_LOGD(TAG, "Checking for updates");

      auto response = HTTP::FirmwareCDN::GetFirmwareVersion(config.updateChannel);
      if (response.result != HTTP::RequestResult::Success) {
        OS_LOGE(TAG, "Failed to fetch firmware version");
        continue;
      }
      version = response.data;
    }

    std::string versionStr = version.toString();

    if (versionStr == OPENSHOCK_FW_VERSION ""sv) {
      OS_LOGI(TAG, "Requested version is already installed");
      continue;
    }

    OS_LOGI(TAG, "Starting update to version: %.*s", versionStr.length(), versionStr.data());

    auto client = std::make_unique<OtaUpdateClient>(version);
    if (!client->Start()) {
      OS_LOGE(TAG, "Failed to start OTA update client");
      continue;
    }

    // Client runs in its own task and will reboot on success.
    // Leak the unique_ptr intentionally — the task owns itself until reboot or failure.
    (void)client.release();

    // Wait a long time before checking again (the client will reboot on success)
    vTaskDelay(pdMS_TO_TICKS(300'000));
  }
}

bool OtaUpdateManager::Init()
{
  esp_err_t err;

  OS_LOGN(TAG, "Fetching current partition");

  const esp_partition_t* partition = esp_ota_get_running_partition();
  if (partition == nullptr) {
    OS_PANIC(TAG, "Failed to get currently running partition");
    return false;
  }

  OS_LOGD(TAG, "Fetching partition state");

  err = esp_ota_get_state_partition(partition, &s_otaImageState);
  if (err != ESP_OK) {
    OS_PANIC(TAG, "Failed to get partition state: %s", esp_err_to_name(err));
    return false;
  }

  OS_LOGD(TAG, "Fetching previous update step");
  OtaUpdateStep updateStep;
  if (!Config::GetOtaUpdateStep(updateStep)) {
    OS_LOGE(TAG, "Failed to get OTA update step");
    return false;
  }

  switch (updateStep) {
    case OtaUpdateStep::Updated:
      s_bootType = FirmwareBootType::NewFirmware;
      break;
    case OtaUpdateStep::Validating:
    case OtaUpdateStep::RollingBack:
      s_bootType = FirmwareBootType::Rollback;
      break;
    default:
      s_bootType = FirmwareBootType::Normal;
      break;
  }

  if (updateStep == OtaUpdateStep::Updated) {
    if (!Config::SetOtaUpdateStep(OtaUpdateStep::Validating)) {
      OS_PANIC(TAG, "Failed to set OTA update step in critical section");
    }
  }

  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, evIpEventHandler, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for IP_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, evWiFiDisconnectedHandler, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for WIFI_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  TaskUtils::TaskCreateExpensive(otaUpdateTask, "OTA Update", 8192, nullptr, 1, &s_taskHandle);  // PROFILED: 6.2KB stack usage

  return true;
}

bool OtaUpdateManager::TryStartFirmwareUpdate(const OpenShock::SemVer& version)
{
  OS_LOGD(TAG, "Requesting firmware update to version %s", version.toString().c_str());

  return tryQueueUpdateRequest(version);
}

FirmwareBootType OtaUpdateManager::GetFirmwareBootType()
{
  return s_bootType;
}

bool OtaUpdateManager::IsValidatingApp()
{
  return s_otaImageState == ESP_OTA_IMG_PENDING_VERIFY;
}

void OtaUpdateManager::InvalidateAndRollback()
{
  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::RollingBack)) {
    OS_PANIC(TAG, "Failed to set OTA firmware boot type in critical section");
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

  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::None)) {
    OS_LOGE(TAG, "Failed to set OTA firmware boot type");
  }

  esp_restart();
}

void OtaUpdateManager::ValidateApp()
{
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    OS_PANIC(TAG, "Unable to mark app as valid");
  }

  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::Validated)) {
    OS_PANIC(TAG, "Failed to set OTA firmware boot type in critical section");
  }

  s_otaImageState = ESP_OTA_IMG_VALID;
}
