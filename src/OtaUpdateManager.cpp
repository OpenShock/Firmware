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

static const char* TAG                          = "OtaUpdateManager";
static OtaUpdateManager::OtaUpdateState s_state = OtaUpdateManager::NONE;

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
  if (states == ESP_OTA_IMG_PENDING_VERIFY) {
    s_state = OtaUpdateManager::FILESYSTEM_PENDING_WIFI;
  }
}

void OtaUpdateManager::Update() { }

OtaUpdateManager::OtaUpdateState OtaUpdateManager::GetState() {
  return s_state;
}

bool OtaUpdateManager::IsUpdateAvailable() {
  return false;
}
bool OtaUpdateManager::IsPerformingUpdate() { }
