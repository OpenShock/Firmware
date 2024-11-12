#include "serialization/WSGateway.h"

const char* const TAG = "WSGateway";

#include <esp_wifi.h>

#include "config/Config.h"
#include "Logging.h"
#include "Time.h"

using namespace OpenShock::Serialization;

bool Gateway::SerializePongMessage(Common::SerializationCallbackFn callback)
{
  int64_t uptime = OpenShock::millis();
  if (uptime < 0) {
    OS_LOGE(TAG, "Failed to get uptime");
    return false;
  }

  int32_t rssi;
  esp_err_t err = esp_wifi_sta_get_rssi(&rssi);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to get WiFi RSSI: %d", err);
    return false;
  }

  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto pong = Gateway::CreatePong(builder, static_cast<uint64_t>(uptime), rssi);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::Pong, pong.Union());

  Gateway::FinishHubToGatewayMessageBuffer(builder, msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeBootStatusMessage(int32_t updateId, OpenShock::FirmwareBootType bootType, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto fbsVersion = Types::CreateSemVerDirect(builder, OPENSHOCK_FW_VERSION_MAJOR, OPENSHOCK_FW_VERSION_MINOR, OPENSHOCK_FW_VERSION_PATCH, OPENSHOCK_FW_VERSION_PRERELEASE, OPENSHOCK_FW_VERSION_BUILD);

  auto fbsBootStatus = Gateway::CreateBootStatus(builder, bootType, fbsVersion, updateId);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::BootStatus, fbsBootStatus.Union());

  Gateway::FinishHubToGatewayMessageBuffer(builder, msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeOtaUpdateStartedMessage(int32_t updateId, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto versionOffset = Types::CreateSemVerDirect(builder, version.major, version.minor, version.patch, version.prerelease.data(), version.build.data());

  auto otaUpdateStartedOffset = Gateway::CreateOtaUpdateStarted(builder, updateId, versionOffset);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::OtaUpdateStarted, otaUpdateStartedOffset.Union());

  Gateway::FinishHubToGatewayMessageBuffer(builder, msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeOtaUpdateProgressMessage(int32_t updateId, Types::OtaUpdateProgressTask task, float progress, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(64);  // TODO: Profile this and adjust the size accordingly

  auto otaUpdateProgressOffset = Gateway::CreateOtaUpdateProgress(builder, updateId, task, progress);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::OtaUpdateProgress, otaUpdateProgressOffset.Union());

  Gateway::FinishHubToGatewayMessageBuffer(builder, msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeOtaUpdateFailedMessage(int32_t updateId, std::string_view message, bool fatal, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto messageOffset = builder.CreateString(message.data(), message.size());

  auto otaUpdateFailedOffset = Gateway::CreateOtaUpdateFailed(builder, updateId, messageOffset, fatal);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::OtaUpdateFailed, otaUpdateFailedOffset.Union());

  Gateway::FinishHubToGatewayMessageBuffer(builder, msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}
