#include "serialization/WSGateway.h"

const char* const TAG = "WSGateway";

#include <esp_wifi.h>

#include "Logging.h"
#include "Temporal.h"

// Firmware version / commit. Regenerated every build, so this file is one of the
// few that recompile on a new commit.
#include "SemVer.h"

using namespace OpenShock::Serialization;

// Wraps a payload in a HubToGatewayMessage, finishes the buffer and hands it to the callback.
template<typename T>
static bool finishMessage(flatbuffers::FlatBufferBuilder& builder, Gateway::HubToGatewayMessagePayload type, flatbuffers::Offset<T> payload, Common::SerializationCallbackFn callback)
{
  auto msg = Gateway::CreateHubToGatewayMessage(builder, type, payload.Union());
  Gateway::FinishHubToGatewayMessageBuffer(builder, msg);
  return callback(builder.GetBufferSpan());
}

bool Gateway::SerializePongMessage(Common::SerializationCallbackFn callback)
{
  int64_t uptime = OpenShock::millis();
  if (uptime < 0) {
    OS_LOGE(TAG, "Failed to get uptime");
    return false;
  }

  // A missing RSSI must not suppress the Pong, or the gateway would treat the hub as unresponsive.
  int rssi      = 0;
  esp_err_t err = esp_wifi_sta_get_rssi(&rssi);
  if (err != ESP_OK) {
    OS_LOGW(TAG, "Failed to get WiFi RSSI: %s", esp_err_to_name(err));
    rssi = 0;
  }

  flatbuffers::FlatBufferBuilder builder(64);

  auto pong = Gateway::CreatePong(builder, static_cast<uint64_t>(uptime), static_cast<int32_t>(rssi));

  return finishMessage(builder, Gateway::HubToGatewayMessagePayload::Pong, pong, callback);
}

bool Gateway::SerializeBootStatusMessage(int32_t updateId, OpenShock::FirmwareBootType bootType, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(128);

  const OpenShock::SemVer& version = OpenShock::Constants::FirmwareVersion();
  auto fbsVersion                  = Types::CreateSemVerDirect(builder, version.major, version.minor, version.patch, version.prerelease.empty() ? nullptr : version.prerelease.c_str(), version.build.empty() ? nullptr : version.build.c_str());

  auto fbsBootStatus = Gateway::CreateBootStatus(builder, static_cast<Types::FirmwareBootType>(bootType), fbsVersion, updateId);

  return finishMessage(builder, Gateway::HubToGatewayMessagePayload::BootStatus, fbsBootStatus, callback);
}

bool Gateway::SerializeOtaUpdateStartedMessage(int32_t updateId, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(128);

  auto versionOffset = Types::CreateSemVerDirect(builder, version.major, version.minor, version.patch, version.prerelease.data(), version.build.data());

  auto otaUpdateStartedOffset = Gateway::CreateOtaUpdateStarted(builder, updateId, versionOffset);

  return finishMessage(builder, Gateway::HubToGatewayMessagePayload::OtaUpdateStarted, otaUpdateStartedOffset, callback);
}

bool Gateway::SerializeOtaUpdateProgressMessage(int32_t updateId, Types::OtaUpdateProgressTask task, float progress, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(64);

  auto otaUpdateProgressOffset = Gateway::CreateOtaUpdateProgress(builder, updateId, task, progress);

  return finishMessage(builder, Gateway::HubToGatewayMessagePayload::OtaUpdateProgress, otaUpdateProgressOffset, callback);
}

bool Gateway::SerializeOtaUpdateFailedMessage(int32_t updateId, std::string_view message, bool fatal, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(128 + message.size());

  auto messageOffset = builder.CreateString(message.data(), message.size());

  auto otaUpdateFailedOffset = Gateway::CreateOtaUpdateFailed(builder, updateId, messageOffset, fatal);

  return finishMessage(builder, Gateway::HubToGatewayMessagePayload::OtaUpdateFailed, otaUpdateFailedOffset, callback);
}
