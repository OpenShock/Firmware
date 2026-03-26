#include <freertos/FreeRTOS.h>

#include "ota/OtaUpdateClient.h"

const char* const TAG = "OtaUpdateClient";

#include "captiveportal/Manager.h"
#include "config/Config.h"
#include "GatewayConnectionManager.h"
#include "http/FirmwareCDN.h"
#include "Logging.h"
#include "ota/OtaUpdateStep.h"
#include "serialization/WSGateway.h"
#include "util/FnProxy.h"
#include "util/HexUtils.h"
#include "util/PartitionUtils.h"
#include "util/TaskUtils.h"

#include <LittleFS.h>

#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include <cstring>

using namespace OpenShock;
using namespace std::string_view_literals;

static bool sendProgressMessage(Serialization::Types::OtaUpdateProgressTask task, float progress)
{
  int32_t updateId;
  if (!Config::GetOtaUpdateId(updateId)) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return false;
  }

  if (!Serialization::Gateway::SerializeOtaUpdateProgressMessage(updateId, task, progress, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to send OTA update progress message");
    return false;
  }

  return true;
}

static bool sendFailureMessage(std::string_view message, bool fatal = false)
{
  int32_t updateId;
  if (!Config::GetOtaUpdateId(updateId)) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return false;
  }

  if (!Serialization::Gateway::SerializeOtaUpdateFailedMessage(updateId, message, fatal, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to send OTA update failed message");
    return false;
  }

  return true;
}

static bool flashAppPartition(const esp_partition_t* partition, std::string_view remoteUrl, const uint8_t (&remoteHash)[32])
{
  OS_LOGD(TAG, "Flashing app partition");

  if (!sendProgressMessage(Serialization::Types::OtaUpdateProgressTask::FlashingApplication, 0.0f)) {
    return false;
  }

  auto onProgress = [](std::size_t current, std::size_t total, float progress) -> bool {
    OS_LOGD(TAG, "Flashing app partition: %u / %u (%.2f%%)", current, total, progress * 100.0f);
    sendProgressMessage(Serialization::Types::OtaUpdateProgressTask::FlashingApplication, progress);
    return true;
  };

  if (!OpenShock::FlashPartitionFromUrl(partition, remoteUrl, remoteHash, onProgress)) {
    OS_LOGE(TAG, "Failed to flash app partition");
    sendFailureMessage("Failed to flash app partition"sv);
    return false;
  }

  if (!sendProgressMessage(Serialization::Types::OtaUpdateProgressTask::MarkingApplicationBootable, 0.0f)) {
    return false;
  }

  if (esp_ota_set_boot_partition(partition) != ESP_OK) {
    OS_LOGE(TAG, "Failed to set app partition bootable");
    sendFailureMessage("Failed to set app partition bootable"sv);
    return false;
  }

  return true;
}

static bool flashFilesystemPartition(const esp_partition_t* partition, std::string_view remoteUrl, const uint8_t (&remoteHash)[32])
{
  if (!sendProgressMessage(Serialization::Types::OtaUpdateProgressTask::PreparingForUpdate, 0.0f)) {
    return false;
  }

  // Make sure captive portal is stopped, timeout after 5 seconds.
  if (!CaptivePortal::ForceClose(5000U)) {
    OS_LOGE(TAG, "Failed to force close captive portal (timed out)");
    sendFailureMessage("Failed to force close captive portal (timed out)"sv);
    return false;
  }

  OS_LOGD(TAG, "Flashing filesystem partition");

  if (!sendProgressMessage(Serialization::Types::OtaUpdateProgressTask::FlashingFilesystem, 0.0f)) {
    return false;
  }

  auto onProgress = [](std::size_t current, std::size_t total, float progress) -> bool {
    OS_LOGD(TAG, "Flashing filesystem partition: %u / %u (%.2f%%)", current, total, progress * 100.0f);
    sendProgressMessage(Serialization::Types::OtaUpdateProgressTask::FlashingFilesystem, progress);
    return true;
  };

  if (!OpenShock::FlashPartitionFromUrl(partition, remoteUrl, remoteHash, onProgress)) {
    OS_LOGE(TAG, "Failed to flash filesystem partition");
    sendFailureMessage("Failed to flash filesystem partition"sv);
    return false;
  }

  if (!sendProgressMessage(Serialization::Types::OtaUpdateProgressTask::VerifyingFilesystem, 0.0f)) {
    return false;
  }

  // Attempt to mount filesystem to verify it's valid.
  fs::LittleFSFS test;
  if (!test.begin(false, "/static", 10, "static0")) {
    OS_LOGE(TAG, "Failed to mount filesystem");
    sendFailureMessage("Failed to mount filesystem"sv);
    return false;
  }
  test.end();

  return true;
}

OtaUpdateClient::OtaUpdateClient(const OpenShock::SemVer& version)
  : m_version(version)
  , m_taskHandle(nullptr)
{
}

OtaUpdateClient::~OtaUpdateClient()
{
  if (m_taskHandle != nullptr) {
    vTaskDelete(m_taskHandle);
  }
}

bool OtaUpdateClient::Start()
{
  if (m_taskHandle != nullptr) {
    OS_LOGE(TAG, "Task already started");
    return false;
  }

  if (TaskUtils::TaskCreateExpensive(Util::FnProxy<&OtaUpdateClient::task>, TAG, 8192, this, 1, &m_taskHandle) != pdPASS) {
    OS_LOGE(TAG, "Failed to create OTA update task");
    return false;
  }

  return true;
}

void OtaUpdateClient::task()
{
  OS_LOGI(TAG, "OTA update task started for version %s", m_version.toString().c_str());

  std::string versionStr = m_version.toString();

  // Generate random int32_t for this update.
  int32_t updateId = static_cast<int32_t>(esp_random());
  if (!Config::SetOtaUpdateId(updateId)) {
    OS_LOGE(TAG, "Failed to set OTA update ID");
    vTaskDelete(nullptr);
    return;
  }
  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::Updating)) {
    OS_LOGE(TAG, "Failed to set OTA update step");
    vTaskDelete(nullptr);
    return;
  }

  if (!Serialization::Gateway::SerializeOtaUpdateStartedMessage(updateId, m_version, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to serialize OTA update started message");
    vTaskDelete(nullptr);
    return;
  }

  if (!sendProgressMessage(Serialization::Types::OtaUpdateProgressTask::FetchingMetadata, 0.0f)) {
    vTaskDelete(nullptr);
    return;
  }

  // Fetch release info from CDN.
  auto response = HTTP::FirmwareCDN::GetFirmwareReleaseInfo(m_version);
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to fetch firmware release info");
    sendFailureMessage("Failed to fetch firmware release info"sv);
    vTaskDelete(nullptr);
    return;
  }

  auto& release = response.data;

  OS_LOGD(TAG, "Firmware release:");
  OS_LOGD(TAG, "  Version:                %.*s", versionStr.length(), versionStr.data());
  OS_LOGD(TAG, "  App binary URL:         %.*s", release.appBinaryUrl.length(), release.appBinaryUrl.data());
  OS_LOGD(TAG, "  App binary hash:        %s", HexUtils::ToHex<32>(release.appBinaryHash).data());
  OS_LOGD(TAG, "  Filesystem binary URL:  %.*s", release.filesystemBinaryUrl.length(), release.filesystemBinaryUrl.data());
  OS_LOGD(TAG, "  Filesystem binary hash: %s", HexUtils::ToHex<32>(release.filesystemBinaryHash).data());

  // Get available app update partition.
  const esp_partition_t* appPartition = esp_ota_get_next_update_partition(nullptr);
  if (appPartition == nullptr) {
    OS_LOGE(TAG, "Failed to get app update partition");
    sendFailureMessage("Failed to get app update partition"sv);
    vTaskDelete(nullptr);
    return;
  }

  // Get filesystem partition.
  const esp_partition_t* filesystemPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "static0");
  if (filesystemPartition == nullptr) {
    OS_LOGE(TAG, "Failed to find filesystem partition");
    sendFailureMessage("Failed to find filesystem partition"sv);
    vTaskDelete(nullptr);
    return;
  }

  // Increase task watchdog timeout to prevent panics when clearing large partitions.
  esp_task_wdt_init(15, true);

  // Flash filesystem first, then app.
  if (!flashFilesystemPartition(filesystemPartition, release.filesystemBinaryUrl, release.filesystemBinaryHash)) {
    esp_task_wdt_init(5, true);
    vTaskDelete(nullptr);
    return;
  }
  if (!flashAppPartition(appPartition, release.appBinaryUrl, release.appBinaryHash)) {
    esp_task_wdt_init(5, true);
    vTaskDelete(nullptr);
    return;
  }

  // Set OTA boot type in config.
  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::Updated)) {
    OS_LOGE(TAG, "Failed to set OTA update step");
    sendFailureMessage("Failed to set OTA update step"sv);
    esp_task_wdt_init(5, true);
    vTaskDelete(nullptr);
    return;
  }

  // Restore task watchdog timeout.
  esp_task_wdt_init(5, true);

  // Send reboot message.
  sendProgressMessage(Serialization::Types::OtaUpdateProgressTask::Rebooting, 0.0f);

  OS_LOGI(TAG, "Restarting into new firmware...");
  vTaskDelay(pdMS_TO_TICKS(200));

  esp_restart();
}
