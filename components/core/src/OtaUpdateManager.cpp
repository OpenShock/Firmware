#include "OtaUpdateManager.h"

const char* const TAG = "OtaUpdateManager";

#include "captiveportal/Manager.h"
#include "config/Config.h"
#include "fs/FsCheck.h"
#include "GatewayConnectionManager.h"
#include "Hashing.h"
#include "http/HTTPRequestManager.h"
#include "hwutil/PartitionUtils.h"
#include "Logging.h"
#include "OpenShock.h"
#include "SemVer.h"
#include "serialization/WSGateway.h"
#include "SimpleMutex.h"
#include "StringHelpers.h"
#include "Temporal.h"
#include "util/HexUtils.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"
#include "json/Json.h"

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>

#include <array>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

/// Repository server firmware API root. Paths per firmware-api-spec.md §4.
#define OPENSHOCK_FW_REPO_API_PREFIX "/2/firmware"

/// @brief Latest release for this board on a channel, with the hub's current version attached so the
/// server can answer 204 when there is nothing to do. See spec §4.2.
#define OPENSHOCK_FW_REPO_LATEST_URL_FORMAT "https://%s" OPENSHOCK_FW_REPO_API_PREFIX "/latest/%s/" OPENSHOCK_FW_BOARD "?version=" OPENSHOCK_FW_VERSION

/// @brief Artifacts for one specific version of this board — the directed-update path. See spec §4.4.
#define OPENSHOCK_FW_REPO_VERSION_URL_FORMAT "https://%s" OPENSHOCK_FW_REPO_API_PREFIX "/versions/%s/" OPENSHOCK_FW_BOARD

enum OtaTaskEventFlag : uint32_t {
  OTA_TASK_EVENT_UPDATE_REQUESTED  = 1 << 0,
  OTA_TASK_EVENT_WIFI_DISCONNECTED = 1 << 1,  // If both connected and disconnected are set, disconnected takes priority.
  OTA_TASK_EVENT_WIFI_CONNECTED    = 1 << 2,
};

static esp_ota_img_states_t _otaImageState;
static OpenShock::FirmwareBootType _bootType;
static TaskHandle_t _taskHandle = nullptr;
static OpenShock::SemVer _requestedVersion;
static OpenShock::SimpleMutex _requestedVersionMutex = {};

using namespace OpenShock;

static bool otaum_try_notify_task(uint32_t eventFlag)
{
  if (_taskHandle == nullptr) {
    OS_LOGW(TAG, "Unable to notify OTA task, task handle is null");
    return false;
  }

  if (xTaskNotify(_taskHandle, eventFlag, eSetBits) != pdPASS) {
    OS_LOGE(TAG, "Failed to notify OTA task (event: 0x%08x)", eventFlag);
    return false;
  }

  return true;
}

static bool otaum_try_queue_update_request(const OpenShock::SemVer& version)
{
  if (!_requestedVersionMutex.lock(pdMS_TO_TICKS(1000))) {
    OS_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  _requestedVersion = version;

  _requestedVersionMutex.unlock();

  otaum_try_notify_task(OTA_TASK_EVENT_UPDATE_REQUESTED);

  return true;
}

static bool _tryGetRequestedVersion(OpenShock::SemVer& version)
{
  if (!_requestedVersionMutex.lock(pdMS_TO_TICKS(1000))) {
    OS_LOGE(TAG, "Failed to take requested version mutex");
    return false;
  }

  version = _requestedVersion;

  _requestedVersionMutex.unlock();

  return true;
}

static void otaum_evh_wifidisconnected(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_id;
  (void)event_data;

  otaum_try_notify_task(OTA_TASK_EVENT_WIFI_DISCONNECTED);
}

static void otaum_evh_ipevent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_data;

  switch (event_id) {
    case IP_EVENT_GOT_IP6:
    case IP_EVENT_STA_GOT_IP:
      otaum_try_notify_task(OTA_TASK_EVENT_WIFI_CONNECTED);
      break;
    case IP_EVENT_STA_LOST_IP:
      otaum_try_notify_task(OTA_TASK_EVENT_WIFI_DISCONNECTED);
      break;
    default:
      return;
  }
}

static bool otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask task, float progress)
{
  int32_t updateId;
  if (!Config::GetOtaUpdateId(updateId)) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return false;
  }

  if (!Serialization::Gateway::SerializeOtaUpdateProgressMessage(updateId, task, progress, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to send OTA install progress message");
    return false;
  }

  return true;
}
static bool _sendFailureMessage(std::string_view message, bool fatal = false)
{
  int32_t updateId;
  if (!Config::GetOtaUpdateId(updateId)) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return false;
  }

  if (!Serialization::Gateway::SerializeOtaUpdateFailedMessage(updateId, message, fatal, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to send OTA install failed message");
    return false;
  }

  return true;
}

static bool otaum_flash_app_partition(OpenShock::HTTP::Client& client, const esp_partition_t* partition, std::string_view remoteUrl, const uint8_t (&remoteHash)[32])
{
  OS_LOGD(TAG, "Flashing app partition");

  if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::FlashingApplication, 0.0f)) {
    return false;
  }

  auto onProgress = [](std::size_t current, std::size_t total, float progress) -> bool {
    OS_LOGD(TAG, "Flashing app partition: %u / %u (%.2f%%)", current, total, progress * 100.0f);

    otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::FlashingApplication, progress);

    return true;
  };

  if (!OpenShock::FlashPartitionFromUrl(client, partition, remoteUrl, remoteHash, onProgress)) {
    OS_LOGE(TAG, "Failed to flash app partition");
    _sendFailureMessage("Failed to flash app partition"sv);
    return false;
  }

  if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::MarkingApplicationBootable, 0.0f)) {
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

static bool otaum_flash_fs_partition(OpenShock::HTTP::Client& client, const esp_partition_t* parition, std::string_view remoteUrl, const uint8_t (&remoteHash)[32])
{
  if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::PreparingForUpdate, 0.0f)) {
    return false;
  }

  // Make sure captive portal is stopped, timeout after 5 seconds.
  if (!CaptivePortal::ForceClose(5000U)) {
    OS_LOGE(TAG, "Failed to force close captive portal (timed out)");
    _sendFailureMessage("Failed to force close captive portal (timed out)"sv);
    return false;
  }

  OS_LOGD(TAG, "Flashing filesystem partition");

  if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::FlashingFilesystem, 0.0f)) {
    return false;
  }

  auto onProgress = [](std::size_t current, std::size_t total, float progress) -> bool {
    OS_LOGD(TAG, "Flashing filesystem partition: %u / %u (%.2f%%)", current, total, progress * 100.0f);

    otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::FlashingFilesystem, progress);

    return true;
  };

  if (!OpenShock::FlashPartitionFromUrl(client, parition, remoteUrl, remoteHash, onProgress)) {
    OS_LOGE(TAG, "Failed to flash filesystem partition");
    _sendFailureMessage("Failed to flash filesystem partition"sv);
    return false;
  }

  if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::VerifyingFilesystem, 0.0f)) {
    return false;
  }

  // Verify the freshly flashed image mounts cleanly.
  if (!OpenShock::TestFilesystem(parition)) {
    OS_LOGE(TAG, "Failed to mount filesystem");
    _sendFailureMessage("Failed to mount filesystem"sv);
    return false;
  }

  return true;
}

static esp_err_t otaum_set_wdt_timeout(uint32_t timeoutMs)
{
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms     = timeoutMs,
    .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,  // Bitmask of all cores
    .trigger_panic  = true,
  };
  // ESP-IDF initializes the TWDT at startup (CONFIG_ESP_TASK_WDT_INIT, default y), so
  // reconfigure it instead of re-initializing. esp_task_wdt_reconfigure() returns
  // ESP_ERR_INVALID_STATE if the TWDT isn't initialized yet, in which case we fall back to init.
  esp_err_t err = esp_task_wdt_reconfigure(&twdt_config);
  if (err == ESP_ERR_INVALID_STATE) {
    // Not yet initialized, fall back to init
    return esp_task_wdt_init(&twdt_config);
  }

  return err;
}

static void otaum_restore_wdt_timeout()
{
  if (otaum_set_wdt_timeout(5) != ESP_OK) {
    OS_LOGE(TAG, "Failed to restore task watchdog timeout");
  }
};

static void otaum_updatetask(void* arg)
{
  (void)arg;

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

    // Reuse a single connection for this update's sequential CDN requests
    // (version -> release -> filesystem binary -> app binary).
    OpenShock::HTTP::Client client;

    OpenShock::SemVer version;
    if (updateRequested) {
      updateRequested = false;

      if (!_tryGetRequestedVersion(version)) {
        OS_LOGE(TAG, "Failed to get requested version");
        continue;
      }
    } else {
      OS_LOGD(TAG, "Checking for updates");

      // Fetch current version.
      if (!OtaUpdateManager::TryGetFirmwareVersion(client, config.updateChannel, version)) {
        OS_LOGE(TAG, "Failed to fetch firmware version");
        continue;
      }
    }

    std::string versionStr = version.toString();  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

    if (versionStr == OPENSHOCK_FW_VERSION ""sv) {
      OS_LOGI(TAG, "Requested version is already installed");
      continue;
    }

    OS_LOGD(TAG, "Updating to version: %.*s", static_cast<int>(versionStr.length()), versionStr.data());

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

    if (!Serialization::Gateway::SerializeOtaUpdateStartedMessage(updateId, version, GatewayConnectionManager::SendMessageBIN)) {
      OS_LOGE(TAG, "Failed to serialize OTA update started message");
      continue;
    }

    if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::FetchingMetadata, 0.0f)) {
      continue;
    }

    // Fetch current release.
    OtaUpdateManager::FirmwareRelease release;
    if (!OtaUpdateManager::TryGetFirmwareRelease(client, version, release)) {
      OS_LOGE(TAG, "Failed to fetch firmware release");  // TODO: Send error message to server
      _sendFailureMessage("Failed to fetch firmware release"sv);
      continue;
    }

    // Print release.
    OS_LOGD(TAG, "Firmware release:");
    OS_LOGD(TAG, "  Version:                %.*s", static_cast<int>(versionStr.length()), versionStr.data());
    OS_LOGD(TAG, "  App binary URL:         %.*s", static_cast<int>(release.appBinaryUrl.length()), release.appBinaryUrl.data());
    OS_LOGD(TAG, "  App binary hash:        %s", HexUtils::ToHex<32>(release.appBinaryHash).data());
    OS_LOGD(TAG, "  Filesystem binary URL:  %.*s", static_cast<int>(release.filesystemBinaryUrl.length()), release.filesystemBinaryUrl.data());
    OS_LOGD(TAG, "  Filesystem binary hash: %s", HexUtils::ToHex<32>(release.filesystemBinaryHash).data());

    // Get available app update partition.
    const esp_partition_t* appPartition = esp_ota_get_next_update_partition(nullptr);
    if (appPartition == nullptr) {
      OS_LOGE(TAG, "Failed to get app update partition");
      _sendFailureMessage("Failed to get app update partition"sv);
      continue;
    }

    // Get filesystem partition.
    const esp_partition_t* filesystemPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "static0");
    if (filesystemPartition == nullptr) {
      OS_LOGE(TAG, "Failed to find filesystem partition");
      _sendFailureMessage("Failed to find filesystem partition"sv);
      continue;
    }

    // Increase task watchdog timeout.
    // Prevents panics on some ESP32s when clearing large partitions.
    if (otaum_set_wdt_timeout(15) != ESP_OK) {
      OS_LOGE(TAG, "Failed to increase task watchdog timeout");
      _sendFailureMessage("Failed to increase task watchdog timeout"sv);
      continue;
    }

    // Flash app and filesystem partitions.
    if (!otaum_flash_fs_partition(client, filesystemPartition, release.filesystemBinaryUrl, release.filesystemBinaryHash)) {
      otaum_restore_wdt_timeout();
      continue;
    }
    if (!otaum_flash_app_partition(client, appPartition, release.appBinaryUrl, release.appBinaryHash)) {
      otaum_restore_wdt_timeout();
      continue;
    }

    // Set OTA boot type in config.
    if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::Updated)) {
      OS_LOGE(TAG, "Failed to set OTA update step");
      _sendFailureMessage("Failed to set OTA update step"sv);
      otaum_restore_wdt_timeout();
      continue;
    }

    // Set task watchdog timeout back to default.
    otaum_restore_wdt_timeout();

    // Send reboot message.
    otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::Rebooting, 0.0f);

    // Reboot into new firmware.
    OS_LOGI(TAG, "Restarting into new firmware...");
    vTaskDelay(pdMS_TO_TICKS(200));
    break;
  }

  // Restart.
  esp_restart();
}

static const char* otaum_channel_name(OpenShock::OtaUpdateChannel channel)
{
  switch (channel) {
    case OtaUpdateChannel::Stable:
      return "stable";
    case OtaUpdateChannel::Beta:
      return "beta";
    case OtaUpdateChannel::Develop:
      return "develop";
    default:
      return nullptr;
  }
}

/// @brief Fetches the repository server domain from runtime config.
///
/// Historically named cdnDomain, from when OTA metadata was flat files served straight off the CDN.
/// It now addresses the repository server's JSON API; artifact URLs come back absolute in the response,
/// so the CDN host no longer has to be known by the hub.
static bool otaum_try_get_repo_domain(std::string& domain)
{
  Config::OtaUpdateConfig config;
  if (!Config::GetOtaUpdateConfig(config)) {
    OS_LOGE(TAG, "Failed to get OTA update config");
    return false;
  }

  if (config.cdnDomain.empty()) {
    OS_LOGE(TAG, "OTA repository domain is not configured");
    return false;
  }

  domain = std::move(config.cdnDomain);

  return true;
}

/// @brief Issues a GET against the repository API and parses the body as JSON.
/// @param body Receives the response body. JsonView is zero-copy, so this must outlive `doc`.
/// @param acceptedCodes Response codes to treat as success. 204 is handled by the caller and must not
///                      reach the parser — an empty body is not valid JSON.
static bool otaum_try_get_json(HTTP::Client& client, std::string_view url, std::span<const uint16_t> acceptedCodes, std::string& body, JSON::JsonDocument& doc, int& code)
{
  auto response = client.GetString(
    url,
    {
      {"Accept", "application/json"}
  },
    acceptedCodes
  );
  if (response.result != OpenShock::HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Request failed: %s [%u] %s", response.ResultToString(), response.code, response.data.c_str());
    return false;
  }

  code = response.code;
  body = std::move(response.data);

  // 204 carries no body; the caller decides what that means for the endpoint it called.
  if (code == 204) {
    return true;
  }

  if (!doc.parse(body)) {
    OS_LOGE(TAG, "Failed to parse response JSON");
    return false;
  }

  return true;
}

bool OtaUpdateManager::Init()
{
  esp_err_t err;

  OS_LOGN(TAG, "Fetching current partition");

  // Fetch current partition info.
  const esp_partition_t* partition = esp_ota_get_running_partition();
  if (partition == nullptr) {
    OS_PANIC(TAG, "Failed to get currently running partition");
    return false;  // Unreachable, here to make tooling happy
  }

  OS_LOGD(TAG, "Fetching partition state");

  // Get OTA state for said partition.
  err = esp_ota_get_state_partition(partition, &_otaImageState);
  if (err != ESP_OK) {
    OS_PANIC(TAG, "Failed to get partition state: %s", esp_err_to_name(err));
    return false;  // Unreachable, here to make tooling happy
  }

  OS_LOGD(TAG, "Fetching previous update step");
  OtaUpdateStep updateStep;
  if (!Config::GetOtaUpdateStep(updateStep)) {
    OS_LOGE(TAG, "Failed to get OTA update step");
    return false;
  }

  // Infer boot type from update step.
  _bootType = OpenShock::InferFirmwareBootType(updateStep);

  if (updateStep == OtaUpdateStep::Updated) {
    if (!Config::SetOtaUpdateStep(OtaUpdateStep::Validating)) {
      OS_PANIC(TAG, "Failed to set OTA update step in critical section");  // TODO: THIS IS A CRITICAL SECTION, WHAT DO WE DO?
    }
  }

  // Start OTA update task.
  if (TaskUtils::TaskCreateExpensive(otaum_updatetask, "OTA Update", 16'384, nullptr, 1, &_taskHandle) != pdPASS) {  // PROFILED: 6.2KB stack usage
    OS_LOGE(TAG, "Failed to create OTA update task");
    return false;
  }

  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, otaum_evh_ipevent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for IP_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, otaum_evh_wifidisconnected, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for WIFI_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

bool OtaUpdateManager::TryGetFirmwareVersion(HTTP::Client& client, OtaUpdateChannel channel, OpenShock::SemVer& version)
{
  const char* channelName = otaum_channel_name(channel);
  if (channelName == nullptr) {
    OS_LOGE(TAG, "Unknown channel: %u", channel);
    return false;
  }

  std::string domain;
  if (!otaum_try_get_repo_domain(domain)) {
    return false;
  }

  std::string uri;
  if (!FormatToString(uri, OPENSHOCK_FW_REPO_LATEST_URL_FORMAT, domain.c_str(), channelName)) {
    OS_LOGE(TAG, "Failed to format URL");
    return false;
  }

  OS_LOGD(TAG, "Fetching latest firmware version from %s", uri.c_str());

  std::string body;
  JSON::JsonDocument doc;
  int code = 0;
  if (!otaum_try_get_json(client, uri, std::array<uint16_t, 3> {200, 204, 304}, body, doc, code)) {
    return false;
  }

  // 204 means the server compared our version against the channel head and found no work to do.
  // Report the running version back so the caller's "already installed" check short-circuits.
  if (code == 204) {
    OS_LOGD(TAG, "Already on the latest version for this channel");
    return OpenShock::TryParseSemVer(OPENSHOCK_FW_VERSION ""sv, version);
  }

  std::string_view versionStr;
  if (!doc.root()["version"].tryGetStr(versionStr)) {
    OS_LOGE(TAG, "Response is missing a 'version' string");
    return false;
  }

  if (!OpenShock::TryParseSemVer(versionStr, version)) {
    OS_LOGE(TAG, "Failed to parse firmware version: %.*s", static_cast<int>(versionStr.size()), versionStr.data());
    return false;
  }

  return true;
}

static bool _tryParseIntoHash(std::string_view hash, uint8_t (&hashBytes)[32])
{
  if (hash.size() != 64) {
    OS_LOGE(TAG, "Invalid hash length %u, expected 64", static_cast<unsigned>(hash.size()));
    return false;
  }

  if (HexUtils::TryParseHex(hash.data(), hash.size(), hashBytes, 32) != 32) {
    OS_LOGE(TAG, "Failed to parse hash: %.*s", static_cast<int>(hash.size()), hash.data());
    return false;
  }

  return true;
}

/// @brief Pulls the app and staticfs artifacts out of a board release response.
///
/// OTA is per-partition, so the hub wants exactly these two. The 'merged' artifact is deliberately
/// ignored: it is a full-flash esptool image (bootloader at 0x1000, partition table at 0x8000, app at
/// 0x10000, filesystem at its offset) meant for USB flashing a blank device. Writing it into an OTA app
/// slot would put a bootloader image where the app belongs. See firmware-api-spec.md §9.2.
static bool otaum_parse_board_artifacts(JSON::JsonView root, OtaUpdateManager::FirmwareRelease& release)
{
  auto artifacts = root["artifacts"];
  if (!artifacts.isArray()) {
    OS_LOGE(TAG, "Response is missing an 'artifacts' array");
    return false;
  }

  bool foundApp = false, foundFilesystem = false;

  int count = artifacts.count();
  for (int i = 0; i < count; i++) {
    auto artifact = artifacts.at(i);

    std::string_view typeStr, urlStr, hashStr;
    if (!artifact["type"].tryGetStr(typeStr) || !artifact["url"].tryGetStr(urlStr) || !artifact["sha256Hash"].tryGetStr(hashStr)) {
      continue;
    }

    if (typeStr == "app"sv) {
      if (foundApp) {
        OS_LOGE(TAG, "Duplicate 'app' artifact");
        return false;
      }
      if (!_tryParseIntoHash(hashStr, release.appBinaryHash)) {
        return false;
      }
      release.appBinaryUrl.assign(urlStr.data(), urlStr.size());
      foundApp = true;
    } else if (typeStr == "staticfs"sv) {
      if (foundFilesystem) {
        OS_LOGE(TAG, "Duplicate 'staticfs' artifact");
        return false;
      }
      if (!_tryParseIntoHash(hashStr, release.filesystemBinaryHash)) {
        return false;
      }
      release.filesystemBinaryUrl.assign(urlStr.data(), urlStr.size());
      foundFilesystem = true;
    }
  }

  if (!foundApp) {
    OS_LOGE(TAG, "Release is missing an 'app' artifact");
    return false;
  }

  if (!foundFilesystem) {
    OS_LOGE(TAG, "Release is missing a 'staticfs' artifact");
    return false;
  }

  return true;
}

bool OtaUpdateManager::TryGetFirmwareRelease(HTTP::Client& client, const OpenShock::SemVer& version, FirmwareRelease& release)
{
  auto versionStr = version.toString();  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

  std::string domain;
  if (!otaum_try_get_repo_domain(domain)) {
    return false;
  }

  std::string uri;
  if (!FormatToString(uri, OPENSHOCK_FW_REPO_VERSION_URL_FORMAT, domain.c_str(), versionStr.c_str())) {
    OS_LOGE(TAG, "Failed to format URL");
    return false;
  }

  OS_LOGD(TAG, "Fetching firmware release from %s", uri.c_str());

  std::string body;
  JSON::JsonDocument doc;
  int code = 0;
  if (!otaum_try_get_json(client, uri, std::array<uint16_t, 2> {200, 304}, body, doc, code)) {
    return false;
  }

  return otaum_parse_board_artifacts(doc.root(), release);
}

bool OtaUpdateManager::TryStartFirmwareUpdate(const OpenShock::SemVer& version)
{
  OS_LOGD(TAG, "Requesting firmware version %s", version.toString().c_str());  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

  return otaum_try_queue_update_request(version);
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
