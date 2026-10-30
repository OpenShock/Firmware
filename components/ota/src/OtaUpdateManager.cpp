#include <freertos/FreeRTOS.h>

#include "OtaUpdateManager.h"

const char* const TAG = "OtaUpdateManager";

#include "AppHooks.h"
#include "BoardDescriptor.h"
#include "captiveportal/Manager.h"
#include "fs/FsCheck.h"
#include "GatewayConnectionManager.h"
#include "Hashing.h"
#include "http/HTTPRequestManager.h"
#include "hwutil/PartitionUtils.h"
#include "Logging.h"
#include "OpenShock.h"
#include "OtaUpdateConfig.h"
#include "SemVer.h"
#include "serialization/WSGateway.h"
#include "SimpleMutex.h"
#include "StringHelpers.h"
#include "Temporal.h"
#include "util/HexUtils.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"
#include "www/WwwPartition.h"
#include "json/Json.h"

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <esp_wifi.h>

#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

// Firmware version / commit. Regenerated every build, so this file is one of the
// few that recompile on a new commit.

using namespace std::string_view_literals;

/// Repository server firmware API root. Paths per firmware-api-spec.md §4.
#define OPENSHOCK_FW_REPO_API_PREFIX "/2/firmware"

// The board in these URLs is the name from the descriptor stamped into the image (Board::Get().board), so the
// repository server's per-board releases keep working unchanged now that the firmware is compiled per chip.

/// @brief Latest release for this board on a channel, with the hub's current version attached so the
/// server can answer 204 when there is nothing to do. See spec §4.2.
/// Arguments: domain, channel, board (as %.*s: length, data), version.
#define OPENSHOCK_FW_REPO_LATEST_URL_FORMAT "https://%s" OPENSHOCK_FW_REPO_API_PREFIX "/latest/%s/%.*s?version=%s"

/// @brief Artifacts for one specific version of this board: the directed-update path. See spec §4.4.
/// Arguments: domain, version, board (as %.*s: length, data).
#define OPENSHOCK_FW_REPO_VERSION_URL_FORMAT "https://%s" OPENSHOCK_FW_REPO_API_PREFIX "/versions/%s/%.*s"

enum OtaTaskEventFlag : uint32_t {
  OTA_TASK_EVENT_UPDATE_REQUESTED  = 1 << 0,
  OTA_TASK_EVENT_WIFI_DISCONNECTED = 1 << 1,  // If both connected and disconnected are set, the current link state decides.
  OTA_TASK_EVENT_WIFI_CONNECTED    = 1 << 2,
  OTA_TASK_EVENT_CHECK_REQUESTED   = 1 << 3,
};

static std::atomic<esp_ota_img_states_t> _otaImageState;
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

  if (xTaskNotifyIndexed(_taskHandle, TaskUtils::kNotifyIndex, eventFlag, eSetBits) != pdPASS) {
    OS_LOGE(TAG, "Failed to notify OTA task (event: 0x%08x)", static_cast<unsigned>(eventFlag));
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

[[nodiscard]] static std::optional<OpenShock::SemVer> _tryGetRequestedVersion()
{
  if (!_requestedVersionMutex.lock(pdMS_TO_TICKS(1000))) {
    OS_LOGE(TAG, "Failed to take requested version mutex");
    return std::nullopt;
  }

  OpenShock::SemVer version = _requestedVersion;

  _requestedVersionMutex.unlock();

  return version;
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
  auto updateId = Config::GetOtaUpdateId();
  if (!updateId) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return false;
  }

  if (!Serialization::Gateway::SerializeOtaUpdateProgressMessage(*updateId, task, progress, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to send OTA install progress message");
    return false;
  }

  return true;
}
static bool _sendFailureMessage(std::string_view message, bool fatal = false)
{
  auto updateId = Config::GetOtaUpdateId();
  if (!updateId) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return false;
  }

  if (!Serialization::Gateway::SerializeOtaUpdateFailedMessage(*updateId, message, fatal, GatewayConnectionManager::SendMessageBIN)) {
    OS_LOGE(TAG, "Failed to send OTA install failed message");
    return false;
  }

  return true;
}

/// @brief Whether the descriptor in `slot` names the board this hub is, logging the verdict.
///
/// A slot with no descriptor at all passes: the repository server keyed pre-descriptor builds by board, and
/// refusing them would block every downgrade to one. A descriptor that is present but corrupt, or a placeholder
/// from an image nobody stamped, is not that case: such an image would run with no pins, so it is refused when
/// strict. Running unstamped ourselves passes, since there is nothing to compare to.
/// @param strict True for an image about to be made bootable: an unreadable, corrupt or unstamped descriptor is a
///               refusal. False for the first-boot check against the slot we came from, where a problem reading
///               the old slot must not roll back a good image.
static bool otaum_slot_matches_this_board(const esp_partition_t* slot, const char* what, bool strict)
{
  uint8_t bytes[OPENSHOCK_BOARD_DESCRIPTOR_SIZE];
  esp_err_t err = esp_partition_read(slot, OPENSHOCK_BOARD_DESCRIPTOR_IMAGE_OFFSET, bytes, sizeof(bytes));
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to read the board descriptor of %s from %s: %s", what, slot->label, esp_err_to_name(err));
    return !strict;
  }

  BoardDescriptor other;
  switch (Board::Parse(bytes, other)) {
    case Board::ParseResult::Absent:
      OS_LOGW(TAG, "%s carries no board descriptor (built before descriptors existed), cannot check it against this board", what);
      return true;
    case Board::ParseResult::Invalid:
      OS_LOGE(TAG, "%s carries a corrupt board descriptor", what);
      return !strict;
    case Board::ParseResult::Unstamped:
      OS_LOGE(TAG, "%s was never stamped with a board and would run with no pins", what);
      return !strict;
    case Board::ParseResult::Valid:
      break;
  }

  if (!Board::IsStamped()) {
    OS_LOGW(TAG, "This image carries no valid board descriptor; accepting %s for %s unchecked", what, other.board);
    return true;
  }

  const BoardDescriptor& self = Board::Get();
  if (std::strcmp(other.board, self.board) != 0 || std::strcmp(other.chip, self.chip) != 0) {
    OS_LOGE(TAG, "%s is for %s (%s), but this hub is %s (%s)", what, other.board, other.chip, self.board, self.chip);
    return false;
  }

  OS_LOGD(TAG, "%s is for %s, matching this hub", what, other.board);
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

  // The hash proved the download is what the server published; this proves the server published it for this
  // board. Checked before the slot is made bootable, so a mislabelled release never gets a chance to boot.
  if (!otaum_slot_matches_this_board(partition, "Downloaded firmware", true)) {
    _sendFailureMessage("Downloaded firmware is for a different board"sv);
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
  OS_LOGD(TAG, "Flashing filesystem partition");

  if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::FlashingFilesystem, 0.0f)) {
    return false;
  }

  auto onProgress = [](std::size_t current, std::size_t total, float progress) -> bool {
    OS_LOGD(TAG, "Flashing filesystem partition: %u / %u (%.2f%%)", current, total, progress * 100.0f);

    otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::FlashingFilesystem, progress);

    return true;
  };

  // Refuse an image in the other format before the first erase: a www image written over littlefs static0 (a 2.x
  // release on the 1.x table, before the www migration has run) or a littlefs image over www would fail the check
  // below only after the portal assets are gone.
  const bool isWwwPartition = parition->type == Www::kPartitionType;
  bool formatMismatch       = false;
  auto matchesPartition     = [isWwwPartition, &formatMismatch](std::span<const uint8_t> chunk) -> bool {
    if (Www::StartsLikeImage(chunk) != isWwwPartition) {
      if (isWwwPartition) {
        OS_LOGE(TAG, "The filesystem image is not a www image");
      } else {
        OS_LOGE(TAG, "The filesystem image is a www image, but this partition table has no www partition");
      }
      formatMismatch = true;
      return false;
    }
    return true;
  };

  if (!OpenShock::FlashPartitionFromUrl(client, parition, remoteUrl, remoteHash, onProgress, matchesPartition)) {
    OS_LOGE(TAG, "Failed to flash filesystem partition");
    _sendFailureMessage(formatMismatch ? "The filesystem image does not match this hub's partition table"sv : "Failed to flash filesystem partition"sv);
    return false;
  }

  if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::VerifyingFilesystem, 0.0f)) {
    return false;
  }

  // Verify the freshly flashed image: the www format where the table has a www partition, else (1.x layout) that the
  // littlefs image mounts.
  const bool valid = isWwwPartition ? Www::VerifyPartition(parition) : OpenShock::TestFilesystem(parition);
  if (!valid) {
    OS_LOGE(TAG, "The flashed filesystem image is not valid");
    _sendFailureMessage("The flashed filesystem image is not valid"sv);
    return false;
  }

  return true;
}

static void otaum_updatetask(void* arg)
{
  (void)arg;

  OS_LOGD(TAG, "OTA update task started");

  bool connected          = false;
  bool updateRequested    = false;
  bool checkRequested     = false;
  int64_t lastUpdateCheck = 0;

  // Update task loop.
  while (true) {
    // Wait for event.
    uint32_t eventBits = 0;
    xTaskNotifyWaitIndexed(TaskUtils::kNotifyIndex, 0, UINT32_MAX, &eventBits, pdMS_TO_TICKS(5000));  // TODO: wait for rest time

    updateRequested |= (eventBits & OTA_TASK_EVENT_UPDATE_REQUESTED) != 0;
    checkRequested |= (eventBits & OTA_TASK_EVENT_CHECK_REQUESTED) != 0;

    bool gotConnected    = (eventBits & OTA_TASK_EVENT_WIFI_CONNECTED) != 0;
    bool gotDisconnected = (eventBits & OTA_TASK_EVENT_WIFI_DISCONNECTED) != 0;

    if (gotConnected && gotDisconnected) {
      // Both edges were coalesced into one wait, so their order is unknown; ask for the current link state.
      connected = WiFiManager::IsConnected();
      OS_LOGD(TAG, "WiFi connection changed, now %s", connected ? "connected" : "disconnected");
    } else if (gotDisconnected) {
      OS_LOGD(TAG, "WiFi disconnected");
      connected = false;
    } else if (gotConnected && !connected) {
      OS_LOGD(TAG, "WiFi connected");
      connected = true;
    }

    // If we're not connected, continue.
    if (!connected) {
      continue;
    }

    int64_t now = OpenShock::millis();

    auto maybeConfig = Config::GetOtaUpdateConfig();
    if (!maybeConfig) {
      OS_LOGE(TAG, "Failed to get OTA update config");
      continue;
    }
    const Config::OtaUpdateConfig& config = *maybeConfig;

    if (!config.isEnabled) {
      OS_LOGD(TAG, "OTA updates are disabled, skipping update check");
      // Dropped rather than kept for later: re-enabling updates shouldn't act on a request made while disabled
      updateRequested = false;
      checkRequested  = false;
      continue;
    }

    if (updateRequested && !config.allowBackendManagement) {
      OS_LOGW(TAG, "Ignoring backend update request, backend management is disabled");
      updateRequested = false;
    }

    bool firstCheck  = lastUpdateCheck == 0;
    int64_t diff     = now - lastUpdateCheck;
    int64_t diffMins = diff / 60'000LL;

    bool check = false;
    check |= config.checkOnStartup && firstCheck;                           // On startup
    check |= config.checkPeriodically && diffMins >= config.checkInterval;  // Periodically
    check |= updateRequested && (firstCheck || diffMins >= 1);              // Update requested
    check |= checkRequested;                                                // Check requested by the user

    if (!check) {
      continue;
    }

    lastUpdateCheck = now;
    checkRequested  = false;

    if (config.requireManualApproval) {
      OS_LOGD(TAG, "Manual approval required, skipping update check");
      updateRequested = false;  // Otherwise it re-triggers this branch every minute
      // TODO: IMPLEMENT
      continue;
    }

    // Reuse a single connection for this update's sequential CDN requests
    // (version -> release -> filesystem binary -> app binary).
    OpenShock::HTTP::Client client;

    OpenShock::SemVer version;
    if (updateRequested) {
      updateRequested = false;

      auto requestedVersion = _tryGetRequestedVersion();
      if (!requestedVersion) {
        OS_LOGE(TAG, "Failed to get requested version");
        continue;
      }
      version = std::move(*requestedVersion);
    } else {
      OS_LOGD(TAG, "Checking for updates");

      // Fetch current version.
      auto latestVersion = OtaUpdateManager::TryGetFirmwareVersion(client, config.updateChannel);
      if (!latestVersion) {
        OS_LOGE(TAG, "Failed to fetch firmware version");
        continue;
      }
      version = std::move(*latestVersion);
    }

    std::string versionStr = version.toString();  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

    if (versionStr == OpenShock::Constants::FW_VERSION) {
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

    // Every `continue` from here on abandons the update; don't leave the step at Updating for the next boot to report.
    // Only resets Updating, so a step another task moved on (or our own Updated below) is left alone.
    struct ResetStepOnFailure {
      bool succeeded = false;
      ~ResetStepOnFailure()
      {
        if (!succeeded) Config::CompareExchangeOtaUpdateStep(OpenShock::OtaUpdateStep::Updating, OpenShock::OtaUpdateStep::None);
      }
    } resetStepOnFailure;

    if (!Serialization::Gateway::SerializeOtaUpdateStartedMessage(updateId, version, GatewayConnectionManager::SendMessageBIN)) {
      OS_LOGE(TAG, "Failed to serialize OTA update started message");
      continue;
    }

    if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::FetchingMetadata, 0.0f)) {
      continue;
    }

    // Fetch current release.
    auto maybeRelease = OtaUpdateManager::TryGetFirmwareRelease(client, version);
    if (!maybeRelease) {
      OS_LOGE(TAG, "Failed to fetch firmware release");  // TODO: Send error message to server
      _sendFailureMessage("Failed to fetch firmware release"sv);
      continue;
    }
    const OtaUpdateManager::FirmwareRelease& release = *maybeRelease;

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

    // The portal assets go into the www partition; on the 1.x layout, which has none, into littlefs static0. The
    // staticfs artifact of a 1.x-numbered release is the littlefs image (scripts/repo_server_publish.py), which is
    // what every hub still on the 1.x layout installs, by this code or by the 1.x firmware's own.
    const esp_partition_t* filesystemPartition = Www::FindPartition();
    if (filesystemPartition == nullptr) {
      filesystemPartition = FindStaticPartition();
    }
    if (filesystemPartition == nullptr) {
      OS_LOGE(TAG, "Failed to find a www or static filesystem partition");
      _sendFailureMessage("Failed to find filesystem partition"sv);
      continue;
    }

    // The captive portal is force-closed for the whole install (it serves from the www partition, and the app slot
    // is swapped under it); on any failure (`continue`) it must be allowed to open again. On success the portal
    // stays closed until the restart below.
    struct ReopenPortalOnFailure {
      bool succeeded = false;
      ~ReopenPortalOnFailure()
      {
        if (!succeeded) CaptivePortal::ReleaseForceClose();
      }
    } reopenPortalOnFailure;

    if (!otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::PreparingForUpdate, 0.0f)) {
      continue;
    }

    // Make sure captive portal is stopped, timeout after 5 seconds.
    if (!CaptivePortal::ForceClose(5000U)) {
      OS_LOGE(TAG, "Failed to force close captive portal (timed out)");
      _sendFailureMessage("Failed to force close captive portal (timed out)"sv);
      continue;
    }

    // Flash app and filesystem partitions. An unchanged www image (the same hash) is not written again; a littlefs
    // static0 always is, as the 1.x firmware does.
    std::array<uint8_t, 32> currentWwwHash {};
    const bool wwwCurrent = filesystemPartition->type == Www::kPartitionType && Www::ImageSha256(filesystemPartition, currentWwwHash) && memcmp(currentWwwHash.data(), release.filesystemBinaryHash, 32) == 0;
    if (wwwCurrent) {
      OS_LOGI(TAG, "The www image is already current, not flashing it");
    } else if (!otaum_flash_fs_partition(client, filesystemPartition, release.filesystemBinaryUrl, release.filesystemBinaryHash)) {
      continue;
    }
    if (!otaum_flash_app_partition(client, appPartition, release.appBinaryUrl, release.appBinaryHash)) {
      continue;
    }

    // Set OTA boot type in config.
    if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::Updated)) {
      OS_LOGE(TAG, "Failed to set OTA update step");
      _sendFailureMessage("Failed to set OTA update step"sv);
      continue;
    }
    resetStepOnFailure.succeeded = true;

    // Send reboot message.
    otaum_send_progress_msg(Serialization::Types::OtaUpdateProgressTask::Rebooting, 0.0f);

    // Reboot into new firmware.
    OS_LOGI(TAG, "Restarting into new firmware...");
    reopenPortalOnFailure.succeeded = true;
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
  }

  return nullptr;  // Channel comes from config and may be out of range
}

/// @brief Fetches the repository server domain from runtime config.
///
/// Historically named cdnDomain, from when OTA metadata was flat files served straight off the CDN.
/// It now addresses the repository server's JSON API; artifact URLs come back absolute in the response,
/// so the CDN host no longer has to be known by the hub.
[[nodiscard]] static std::optional<std::string> otaum_try_get_repo_domain()
{
  auto config = Config::GetOtaUpdateConfig();
  if (!config) {
    OS_LOGE(TAG, "Failed to get OTA update config");
    return std::nullopt;
  }

  if (config->cdnDomain.empty()) {
    OS_LOGE(TAG, "OTA repository domain is not configured");
    return std::nullopt;
  }

  return std::move(config->cdnDomain);
}

/// @brief Issues a GET against the repository API and parses the body as JSON.
/// @param body Receives the response body. JsonView is zero-copy, so this must outlive `doc`.
/// @param acceptedCodes Response codes to treat as success. 204 is handled by the caller and must not
///                      reach the parser; an empty body is not valid JSON.
/// @return The response code, or std::nullopt if the request or the JSON parse failed.
[[nodiscard]] static std::optional<int> otaum_try_get_json(HTTP::Client& client, std::string_view url, std::span<const uint16_t> acceptedCodes, std::string& body, JSON::JsonDocument& doc)
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
    return std::nullopt;
  }

  int code = response.code;
  body     = std::move(response.data);

  // 204 carries no body; the caller decides what that means for the endpoint it called.
  if (code == 204) {
    return code;
  }

  if (!doc.parse(body)) {
    OS_LOGE(TAG, "Failed to parse response JSON");
    return std::nullopt;
  }

  return code;
}

bool OtaUpdateManager::Init()
{
  AppHooks::RegisterOtaUpdateManager(OtaUpdateManager::GetFirmwareBootType, OtaUpdateManager::TryStartFirmwareUpdate, OtaUpdateManager::RequestUpdateCheck);

  esp_err_t err;

  OS_LOGD(TAG, "Fetching current partition");

  // Fetch current partition info.
  const esp_partition_t* partition = esp_ota_get_running_partition();
  if (partition == nullptr) {
    OS_PANIC(TAG, "Failed to get currently running partition");
    return false;  // Unreachable, here to make tooling happy
  }

  OS_LOGD(TAG, "Fetching partition state");

  // Get OTA state for said partition.
  esp_ota_img_states_t imageState;
  err = esp_ota_get_state_partition(partition, &imageState);
  if (err != ESP_OK) {
    OS_PANIC(TAG, "Failed to get partition state: %s", esp_err_to_name(err));
    return false;  // Unreachable, here to make tooling happy
  }
  _otaImageState = imageState;

  OS_LOGD(TAG, "Fetching previous update step");
  auto maybeUpdateStep = Config::GetOtaUpdateStep();
  if (!maybeUpdateStep) {
    OS_LOGE(TAG, "Failed to get OTA update step");
    return false;
  }
  OtaUpdateStep updateStep = *maybeUpdateStep;

  // Infer boot type from update step.
  _bootType = OpenShock::InferFirmwareBootType(updateStep);

  if (updateStep == OtaUpdateStep::Updated) {
    if (!Config::SetOtaUpdateStep(OtaUpdateStep::Validating)) {
      OS_PANIC(TAG, "Failed to set OTA update step in critical section");  // TODO: THIS IS A CRITICAL SECTION, WHAT DO WE DO?
    }
  }

  // Start OTA update task.
  if (TaskUtils::TaskCreateExpensive(otaum_updatetask, "OTA Update", CONFIG_OPENSHOCK_OTA_TASK_STACK_SIZE, nullptr, 1, &_taskHandle) != pdPASS) {
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

std::optional<OpenShock::SemVer> OtaUpdateManager::TryGetFirmwareVersion(HTTP::Client& client, OtaUpdateChannel channel)
{
  const char* channelName = otaum_channel_name(channel);
  if (channelName == nullptr) {
    OS_LOGE(TAG, "Unknown channel: %u", std::to_underlying(channel));
    return std::nullopt;
  }

  auto domain = otaum_try_get_repo_domain();
  if (!domain) {
    return std::nullopt;
  }

  std::string uri;
  const auto board = Board::Name();
  if (!FormatToString(uri, OPENSHOCK_FW_REPO_LATEST_URL_FORMAT, domain->c_str(), channelName, static_cast<int>(board.size()), board.data(), OpenShock::Constants::FW_VERSION)) {
    OS_LOGE(TAG, "Failed to format URL");
    return std::nullopt;
  }

  OS_LOGD(TAG, "Fetching latest firmware version from %s", uri.c_str());

  std::string body;
  JSON::JsonDocument doc;
  auto code = otaum_try_get_json(client, uri, std::array<uint16_t, 2> {200, 204}, body, doc);
  if (!code) {
    return std::nullopt;
  }

  // 204 means the server compared our version against the channel head and found no work to do.
  // Report the running version back so the caller's "already installed" check short-circuits.
  if (*code == 204) {
    OS_LOGD(TAG, "Already on the latest version for this channel");
    return OpenShock::Constants::FirmwareVersion();
  }

  auto versionStr = doc.root()["version"].tryGetStr();
  if (!versionStr) {
    OS_LOGE(TAG, "Response is missing a 'version' string");
    return std::nullopt;
  }

  auto version = OpenShock::TryParseSemVer(*versionStr);
  if (!version) {
    OS_LOGE(TAG, "Failed to parse firmware version: %.*s", static_cast<int>(versionStr->size()), versionStr->data());
    return std::nullopt;
  }

  return version;
}

static bool _tryParseIntoHash(std::string_view hash, uint8_t (&hashBytes)[32])
{
  if (hash.size() != 64) {
    OS_LOGE(TAG, "Invalid hash length %u, expected 64", static_cast<unsigned>(hash.size()));
    return false;
  }

  if (HexUtils::TryParseHex(hash, std::span<uint8_t>(hashBytes)) != 32) {
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
[[nodiscard]] static std::optional<OtaUpdateManager::FirmwareRelease> otaum_parse_board_artifacts(JSON::JsonView root)
{
  auto artifacts = root["artifacts"];
  if (!artifacts.isArray()) {
    OS_LOGE(TAG, "Response is missing an 'artifacts' array");
    return std::nullopt;
  }

  OtaUpdateManager::FirmwareRelease release {};

  bool foundApp = false, foundFilesystem = false;

  int count = artifacts.count();
  for (int i = 0; i < count; i++) {
    auto artifact = artifacts.at(i);

    auto typeStr = artifact["type"].tryGetStr();
    if (!typeStr) {
      continue;
    }
    auto urlStr = artifact["url"].tryGetStr();
    if (!urlStr) {
      continue;
    }
    auto hashStr = artifact["sha256Hash"].tryGetStr();
    if (!hashStr) {
      continue;
    }

    if (*typeStr == "app"sv) {
      if (foundApp) {
        OS_LOGE(TAG, "Duplicate 'app' artifact");
        return std::nullopt;
      }
      if (!_tryParseIntoHash(*hashStr, release.appBinaryHash)) {
        return std::nullopt;
      }
      release.appBinaryUrl = std::move(*urlStr);
      foundApp             = true;
    } else if (*typeStr == "staticfs"sv) {
      if (foundFilesystem) {
        OS_LOGE(TAG, "Duplicate 'staticfs' artifact");
        return std::nullopt;
      }
      if (!_tryParseIntoHash(*hashStr, release.filesystemBinaryHash)) {
        return std::nullopt;
      }
      release.filesystemBinaryUrl = std::move(*urlStr);
      foundFilesystem             = true;
    }
  }

  if (!foundApp) {
    OS_LOGE(TAG, "Release is missing an 'app' artifact");
    return std::nullopt;
  }

  if (!foundFilesystem) {
    OS_LOGE(TAG, "Release is missing a 'staticfs' artifact");
    return std::nullopt;
  }

  return release;
}

std::optional<OtaUpdateManager::FirmwareRelease> OtaUpdateManager::TryGetFirmwareRelease(HTTP::Client& client, const OpenShock::SemVer& version)
{
  auto versionStr = version.toString();  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

  auto domain = otaum_try_get_repo_domain();
  if (!domain) {
    return std::nullopt;
  }

  std::string uri;
  const auto board = Board::Name();
  if (!FormatToString(uri, OPENSHOCK_FW_REPO_VERSION_URL_FORMAT, domain->c_str(), versionStr.c_str(), static_cast<int>(board.size()), board.data())) {
    OS_LOGE(TAG, "Failed to format URL");
    return std::nullopt;
  }

  OS_LOGD(TAG, "Fetching firmware release from %s", uri.c_str());

  std::string body;
  JSON::JsonDocument doc;
  if (!otaum_try_get_json(client, uri, std::array<uint16_t, 1> {200}, body, doc)) {
    return std::nullopt;
  }

  return otaum_parse_board_artifacts(doc.root());
}

bool OtaUpdateManager::TryStartFirmwareUpdate(const OpenShock::SemVer& version)
{
  OS_LOGD(TAG, "Requesting firmware version %s", version.toString().c_str());  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

  return otaum_try_queue_update_request(version);
}

bool OtaUpdateManager::RequestUpdateCheck()
{
  return otaum_try_notify_task(OTA_TASK_EVENT_CHECK_REQUESTED);
}

FirmwareBootType OtaUpdateManager::GetFirmwareBootType()
{
  return _bootType;
}

bool OtaUpdateManager::IsValidatingApp()
{
  return _otaImageState == ESP_OTA_IMG_PENDING_VERIFY;
}

bool OtaUpdateManager::IsPendingImageForThisBoard()
{
  // The slot we were installed from is the other OTA slot, which is also the next update target.
  const esp_partition_t* previous = esp_ota_get_next_update_partition(nullptr);
  if (previous == nullptr) {
    OS_LOGW(TAG, "No previous app slot to check the board against");
    return true;
  }

  return otaum_slot_matches_this_board(previous, "The previous firmware", false);
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
