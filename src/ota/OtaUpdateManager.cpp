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
#include "SimpleMutex.h"
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

enum OtaTaskEventFlag : uint32_t {
  OTA_TASK_EVENT_WIFI_DISCONNECTED = 1 << 0,  // If both connected and disconnected are set, disconnected takes priority.
  OTA_TASK_EVENT_WIFI_CONNECTED    = 1 << 1,
};

static esp_ota_img_states_t s_otaImageState;
static OpenShock::FirmwareBootType s_bootType;
static TaskHandle_t s_taskHandle                            = nullptr;
static OpenShock::SimpleMutex s_clientMtx                   = {};
static std::unique_ptr<OpenShock::OtaUpdateClient> s_client = nullptr;

using namespace OpenShock;

static bool tryStartUpdate(const OpenShock::SemVer& version)
{
  if (!s_clientMtx.lock(pdMS_TO_TICKS(1000))) {
    OS_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  if (s_client != nullptr) {
    s_clientMtx.unlock();
    OS_LOGE(TAG, "Update client already started");
    return false;
  }

  s_client = std::make_unique<OtaUpdateClient>(version);

  if (!s_client->Start()) {
    s_client.reset();
    s_clientMtx.unlock();
    OS_LOGE(TAG, "Failed to start update client");
    return false;
  }

  s_clientMtx.unlock();

  OS_LOGD(TAG, "Update client started");

  return true;
}

static void wifiDisconnectedEventHandler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_id;
  (void)event_data;

  xTaskNotify(s_taskHandle, OTA_TASK_EVENT_WIFI_DISCONNECTED, eSetBits);
}

static void ipEventHandler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
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

static void watcherTask(void*)
{
  OS_LOGD(TAG, "OTA update task started");

  bool connected          = false;
  int64_t lastUpdateCheck = 0;

  // Update task loop.
  while (true) {
    // Wait for event.
    uint32_t eventBits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &eventBits, pdMS_TO_TICKS(5000));  // TODO: wait for rest time

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

    if (!check) {
      continue;
    }

    lastUpdateCheck = now;

    if (config.requireManualApproval) {
      OS_LOGD(TAG, "Manual approval required, skipping update check");
      // TODO: IMPLEMENT
      continue;
    }

    OS_LOGD(TAG, "Checking for updates");

    // Fetch current version.
    auto result = HTTP::FirmwareCDN::GetFirmwareVersion(config.updateChannel);
    if (result.result != HTTP::RequestResult::Success) {
      OS_LOGE(TAG, "Failed to fetch firmware version");
      continue;
    }

    OS_LOGD(TAG, "Remote version: %s", result.data.toString().c_str());  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this
  }
}

bool OtaUpdateManager::Init()
{
  esp_err_t err;

  OS_LOGN(TAG, "Fetching current partition");

  // Fetch current partition info.
  const esp_partition_t* partition = esp_ota_get_running_partition();
  if (partition == nullptr) {
    OS_PANIC(TAG, "Failed to get currently running partition");
    return false;  // This will never be reached, but the compiler doesn't know that.
  }

  OS_LOGD(TAG, "Fetching partition state");

  // Get OTA state for said partition.
  err = esp_ota_get_state_partition(partition, &s_otaImageState);
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
      s_bootType = FirmwareBootType::NewFirmware;
      break;
    case OtaUpdateStep::Validating:  // If the update step is validating, we have failed in the middle of validating the new firmware, meaning this is a rollback.
    case OtaUpdateStep::RollingBack:
      s_bootType = FirmwareBootType::Rollback;
      break;
    default:
      s_bootType = FirmwareBootType::Normal;
      break;
  }

  if (updateStep == OtaUpdateStep::Updated) {
    if (!Config::SetOtaUpdateStep(OtaUpdateStep::Validating)) {
      OS_PANIC(TAG, "Failed to set OTA update step in critical section");  // TODO: THIS IS A CRITICAL SECTION, WHAT DO WE DO?
    }
  }

  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ipEventHandler, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for IP_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifiDisconnectedEventHandler, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for WIFI_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  if (TaskUtils::TaskCreateExpensive(watcherTask, "OtaWatcherTask", 8192, nullptr, 1, &s_taskHandle) != pdPASS) {
    OS_LOGE(TAG, "Failed to create OTA watcher task");
    return false;
  }

  return true;
}

bool OtaUpdateManager::TryStartFirmwareUpdate(const OpenShock::SemVer& version)
{
  if (version == OPENSHOCK_FW_VERSION ""sv) {
    OS_LOGI(TAG, "Requested version is already installed");
    return true;
  }

  OS_LOGD(TAG, "Requesting firmware version %s", version.toString().c_str());  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

  return tryStartUpdate(version);
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

  s_otaImageState = ESP_OTA_IMG_VALID;
}
