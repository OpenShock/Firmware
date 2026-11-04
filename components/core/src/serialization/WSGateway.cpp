#include "serialization/WSGateway.h"

const char* const TAG = "WSGateway";

#include <esp_wifi.h>
#include <optional>
#include <utility>

#include "Logging.h"
#include "OpenShock.h"
#include "SemVer.h"
#include "Temporal.h"

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

// Exhaustive with no default on purpose: -Werror=switch fails the build when FirmwareBootType gains a value that is not
// mapped here. Any other value (a caller can pass anything) falls out as nullopt.
static std::optional<Types::FirmwareBootType> toSchemaBootType(OpenShock::FirmwareBootType bootType)
{
  switch (bootType) {
    case OpenShock::FirmwareBootType::Normal:
      return Types::FirmwareBootType::Normal;
    case OpenShock::FirmwareBootType::NewFirmware:
      return Types::FirmwareBootType::NewFirmware;
    case OpenShock::FirmwareBootType::Rollback:
      return Types::FirmwareBootType::Rollback;
  }
  return std::nullopt;
}

// Empty prerelease/build stay absent from the message rather than going out as "" (both messages the same way)
static flatbuffers::Offset<Types::SemVer> createSemVer(flatbuffers::FlatBufferBuilder& builder, const OpenShock::SemVer& version)
{
  flatbuffers::Offset<flatbuffers::String> prerelease, build;
  if (!version.prerelease.empty()) {
    prerelease = builder.CreateString(version.prerelease);
  }
  if (!version.build.empty()) {
    build = builder.CreateString(version.build);
  }
  return Types::CreateSemVer(builder, version.major, version.minor, version.patch, prerelease, build);
}

bool Gateway::SerializeBootStatusMessage(int32_t updateId, OpenShock::FirmwareBootType bootType, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(128);

  auto fbsVersion = createSemVer(builder, OpenShock::Constants::FirmwareVersion());

  auto fbsBootType = toSchemaBootType(bootType);
  if (!fbsBootType) {
    // Normal is what AppHooks reports when it can't tell; not sending a boot status at all would leave the gateway
    // without one
    OS_LOGE(TAG, "Unknown firmware boot type %u, reporting normal", static_cast<unsigned>(std::to_underlying(bootType)));
  }

  auto fbsBootStatus = Gateway::CreateBootStatus(builder, fbsBootType.value_or(Types::FirmwareBootType::Normal), fbsVersion, updateId);

  return finishMessage(builder, Gateway::HubToGatewayMessagePayload::BootStatus, fbsBootStatus, callback);
}

bool Gateway::SerializeOtaUpdateStartedMessage(int32_t updateId, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(128);

  auto versionOffset = createSemVer(builder, version);

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
