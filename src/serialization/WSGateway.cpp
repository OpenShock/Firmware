#include "serialization/WSGateway.h"

#include "config/Config.h"
#include "Logging.h"
#include "Time.h"

const char* const TAG = "WSGateway";

using namespace OpenShock::Serialization;

bool Gateway::SerializeKeepAliveMessage(Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  int64_t uptime = OpenShock::millis();
  if (uptime < 0) {
    ESP_LOGE(TAG, "Failed to get uptime");
    return false;
  }

  Gateway::KeepAlive keepAlive(static_cast<uint64_t>(uptime));
  auto keepAliveOffset = builder.CreateStruct(keepAlive);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::KeepAlive, keepAliveOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeBootStatusMessage(int32_t updateId, OpenShock::FirmwareBootType bootType, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto fbsVersion = Types::CreateSemVerDirect(builder, version.major, version.minor, version.patch, version.prerelease.data(), version.build.data());

  auto fbsBootStatus = Gateway::CreateBootStatus(builder, bootType, fbsVersion, updateId);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::BootStatus, fbsBootStatus.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeOtaInstallStartedMessage(int32_t updateId, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto versionOffset = Types::CreateSemVerDirect(builder, version.major, version.minor, version.patch, version.prerelease.data(), version.build.data());

  auto otaInstallStartedOffset = Gateway::CreateOtaInstallStarted(builder, updateId, versionOffset);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::OtaInstallStarted, otaInstallStartedOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeOtaInstallProgressMessage(int32_t updateId, Gateway::OtaInstallProgressTask task, float progress, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(64);  // TODO: Profile this and adjust the size accordingly

  auto otaInstallProgressOffset = Gateway::CreateOtaInstallProgress(builder, updateId, task, progress);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::OtaInstallProgress, otaInstallProgressOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeOtaInstallFailedMessage(int32_t updateId, StringView message, bool fatal, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto messageOffset = builder.CreateString(message.data(), message.size());

  auto otaInstallFailedOffset = Gateway::CreateOtaInstallFailed(builder, updateId, messageOffset, fatal);

  auto msg = Gateway::CreateHubToGatewayMessage(builder, Gateway::HubToGatewayMessagePayload::OtaInstallFailed, otaInstallFailedOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}
