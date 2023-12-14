#include "OtaUpdateManager.h"

#include "config/Config.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "wifi/WiFiManager.h"

#include <esp_ota_ops.h>

#include <LittleFS.h>
#include <WiFi.h>

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

static bool _otaValidatingImage = false;

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
  _otaValidatingImage = states == ESP_OTA_IMG_PENDING_VERIFY;
}

bool OtaUpdateManager::IsValidatingImage() {
  return _otaValidatingImage;
}

void OtaUpdateManager::InvalidateAndRollback() {
  esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();

  // If we get here, something went VERY wrong.
  // TODO: Wtf do we do here?

  // I have no idea, placeholder:

  vTaskDelay(pdMS_TO_TICKS(5000));

  esp_restart();
}

void OtaUpdateManager::ValidateImage() {
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    ESP_PANIC(TAG, "Unable to mark app as valid, WTF?");  // TODO: Wtf do we do here?
  }
}
