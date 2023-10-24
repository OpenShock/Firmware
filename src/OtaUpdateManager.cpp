#include "OtaUpdateManager.h"
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <WiFi.h>

using namespace OpenShock;

/*
  static bool s_wifiConnected                  = false;
  static bool s_wifiConnecting                 = false;
  static std::uint8_t s_connectedCredentialsID = 0;
  static std::uint8_t s_preferredCredentialsID = 0;
  static std::vector<WiFiNetwork> s_wifiNetworks;
*/

static const char* TAG                       = "OtaUpdateManager";
static OtaUpdateManager::BootMode s_bootMode = OtaUpdateManager::BootMode::NORMAL;
static OtaUpdateManager::UpdateState s_state = OtaUpdateManager::UpdateState::NONE;

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
  s_bootMode = OtaUpdateManager::BootMode::NORMAL;
  if (states == ESP_OTA_IMG_PENDING_VERIFY) {
    s_bootMode = OtaUpdateManager::BootMode::OTA_UPDATE;
  }
}

void OtaUpdateManager::Setup() { }

void OtaUpdateManager::Loop() { }

bool OtaUpdateManager::IsPerformingUpdate() {
  return s_bootMode == OtaUpdateManager::BootMode::OTA_UPDATE;
}

OtaUpdateManager::BootMode OtaUpdateManager::GetBootMode() {
  return s_bootMode;
}

OtaUpdateManager::UpdateState OtaUpdateManager::GetState() {
  return s_state;
}
