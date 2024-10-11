#include <freertos/FreeRTOS.h>

#include "CaptivePortal.h"
#include "config/Config.h"
#include "GatewayConnectionManager.h"
#include "http/FirmwareCDN.h"
#include "Logging.h"
#include "ota/FirmwareReleaseInfo.h"
#include "ota/OtaUpdateClient.h"
#include "ota/OtaUpdateManager.h"
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
#include <freertos/semphr.h>

#include <cstring>

const char* const TAG = "OtaUpdateClient";

using namespace OpenShock;
using namespace std::string_view_literals;

bool _tryStartUpdate(const OpenShock::SemVer& version)
{
  if (xSemaphoreTake(_requestedVersionMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    OS_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  _requestedVersion = version;

  xSemaphoreGive(_requestedVersionMutex);

  xTaskNotify(_taskHandle, OTA_TASK_EVENT_UPDATE_REQUESTED, eSetBits);

  return true;
}

bool _tryGetRequestedVersion(OpenShock::SemVer& version)
{
  if (xSemaphoreTake(_requestedVersionMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    OS_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  version = _requestedVersion;

  xSemaphoreGive(_requestedVersionMutex);

  return true;
}

bool _sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask task, float progress)
{
  int32_t updateId;
  if (!Config::GetOtaUpdateId(updateId)) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return false;
  }

  if (!Serialization::Gateway::SerializeOtaInstallProgressMessage(updateId, task, progress, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to send OTA install progress message");
    return false;
  }

  return true;
}
bool _sendFailureMessage(std::string_view message, bool fatal = false)
{
  int32_t updateId;
  if (!Config::GetOtaUpdateId(updateId)) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return false;
  }

  if (!Serialization::Gateway::SerializeOtaInstallFailedMessage(updateId, message, fatal, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to send OTA install failed message");
    return false;
  }

  return true;
}

bool _flashAppPartition(const esp_partition_t* partition, std::string_view remoteUrl, const uint8_t (&remoteHash)[32])
{
  OS_LOGD(TAG, "Flashing app partition");

  if (!_sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask::FlashingApplication, 0.0f)) {
    return false;
  }

  auto onProgress = [](std::size_t current, std::size_t total, float progress) -> bool {
    OS_LOGD(TAG, "Flashing app partition: %u / %u (%.2f%%)", current, total, progress * 100.0f);

    _sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask::FlashingApplication, progress);

    return true;
  };

  if (!OpenShock::FlashPartitionFromUrl(partition, remoteUrl, remoteHash, onProgress)) {
    OS_LOGE(TAG, "Failed to flash app partition");
    _sendFailureMessage("Failed to flash app partition"sv);
    return false;
  }

  if (!_sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask::MarkingApplicationBootable, 0.0f)) {
    return false;
  }

  // Set app partition bootable.
  if (esp_ota_set_boot_partition(partition) != ESP_OK) {
    OS_LOGE(TAG, "Failed to set app partition bootable");
    _sendFailureMessage("Failed to set app partition bootable"sv);
    return false;
  }

  return true;
}

bool _flashFilesystemPartition(const esp_partition_t* parition, std::string_view remoteUrl, const uint8_t (&remoteHash)[32])
{
  if (!_sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask::PreparingForInstall, 0.0f)) {
    return false;
  }

  // Make sure captive portal is stopped, timeout after 5 seconds.
  if (!CaptivePortal::ForceClose(5000U)) {
    OS_LOGE(TAG, "Failed to force close captive portal (timed out)");
    _sendFailureMessage("Failed to force close captive portal (timed out)"sv);
    return false;
  }

  OS_LOGD(TAG, "Flashing filesystem partition");

  if (!_sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask::FlashingFilesystem, 0.0f)) {
    return false;
  }

  auto onProgress = [](std::size_t current, std::size_t total, float progress) -> bool {
    OS_LOGD(TAG, "Flashing filesystem partition: %u / %u (%.2f%%)", current, total, progress * 100.0f);

    _sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask::FlashingFilesystem, progress);

    return true;
  };

  if (!OpenShock::FlashPartitionFromUrl(parition, remoteUrl, remoteHash, onProgress)) {
    OS_LOGE(TAG, "Failed to flash filesystem partition");
    _sendFailureMessage("Failed to flash filesystem partition"sv);
    return false;
  }

  if (!_sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask::VerifyingFilesystem, 0.0f)) {
    return false;
  }

  // Attempt to mount filesystem.
  fs::LittleFSFS test;
  if (!test.begin(false, "/static", 10, "static0")) {
    OS_LOGE(TAG, "Failed to mount filesystem");
    _sendFailureMessage("Failed to mount filesystem"sv);
    return false;
  }
  test.end();

  OpenShock::CaptivePortal::ForceClose(false);

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

  if (TaskUtils::TaskCreateExpensive(&Util::FnProxy<&OtaUpdateClient::_task>, TAG, 8192, this, 1, &m_taskHandle) != pdPASS) {
    OS_LOGE(TAG, "Failed to create OTA update task");
    return false;
  }

  return true;
}

void OtaUpdateClient::_task()
{
  // Generate random int32_t for this update.
  int32_t updateId = static_cast<int32_t>(esp_random());
  if (!Config::SetOtaUpdateId(updateId)) {
    OS_LOGE(TAG, "Failed to set OTA update ID");
    continue;
  }
  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::Updating)) {
    OS_LOGE(TAG, "Failed to set OTA update step");
    continue;
  }

  if (!Serialization::Gateway::SerializeOtaInstallStartedMessage(updateId, m_version, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to serialize OTA install started message");
    continue;
  }

  if (!_sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask::FetchingMetadata, 0.0f)) {
    continue;
  }

  // Fetch current release.
  auto response = HTTP::FirmwareCDN::GetFirmwareReleaseInfo(m_version);
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to fetch firmware release: [%u]", response.code);
    _sendFailureMessage("Failed to fetch firmware release"sv);
    continue;
  }

  auto& release = response.data;

  // Print release.
  OS_LOGD(TAG, "Firmware release:");
  OS_LOGD(TAG, "  Version:                %s", m_version.toString().c_str());  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this
  OS_LOGD(TAG, "  App binary URL:         %s", release.appBinaryUrl.c_str());
  OS_LOGD(TAG, "  App binary hash:        %s", HexUtils::ToHex<32>(release.appBinaryHash).data());
  OS_LOGD(TAG, "  Filesystem binary URL:  %s", release.filesystemBinaryUrl.c_str());
  OS_LOGD(TAG, "  Filesystem binary hash: %s", HexUtils::ToHex<32>(release.filesystemBinaryHash).data());

  // Get available app update partition.
  const esp_partition_t* appPartition = esp_ota_get_next_update_partition(nullptr);
  if (appPartition == nullptr) {
    OS_LOGE(TAG, "Failed to get app update partition");  // TODO: Send error message to server
    _sendFailureMessage("Failed to get app update partition"sv);
    continue;
  }

  // Get filesystem partition.
  const esp_partition_t* filesystemPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "static0");
  if (filesystemPartition == nullptr) {
    OS_LOGE(TAG, "Failed to find filesystem partition");  // TODO: Send error message to server
    _sendFailureMessage("Failed to find filesystem partition"sv);
    continue;
  }

  // Increase task watchdog timeout.
  // Prevents panics on some ESP32s when clearing large partitions.
  esp_task_wdt_init(15, true);

  // Flash app and filesystem partitions.
  if (!_flashFilesystemPartition(filesystemPartition, release.filesystemBinaryUrl, release.filesystemBinaryHash)) continue;
  if (!_flashAppPartition(appPartition, release.appBinaryUrl, release.appBinaryHash)) continue;

  // Set OTA boot type in config.
  if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::Updated)) {
    OS_LOGE(TAG, "Failed to set OTA update step");
    _sendFailureMessage("Failed to set OTA update step"sv);
    continue;
  }

  // Set task watchdog timeout back to default.
  esp_task_wdt_init(5, true);

  // Send reboot message.
  _sendProgressMessage(Serialization::Gateway::OtaInstallProgressTask::Rebooting, 0.0f);

  // Reboot into new firmware.
  OS_LOGI(TAG, "Restarting into new firmware...");
  vTaskDelay(pdMS_TO_TICKS(200));
  break;

  // Restart.
  esp_restart();
}
