#include "serialization/WSGateway.h"

#include "Logging.h"
#include "Time.h"

const char* const TAG = "WSGateway";

using namespace OpenShock::Serialization;

bool Gateway::SerializeKeepAliveMessage(Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  std::int64_t uptime = OpenShock::millis();
  if (uptime < 0) {
    ESP_LOGE(TAG, "Failed to get uptime");
    return false;
  }

  Gateway::KeepAlive keepAlive(static_cast<std::uint64_t>(uptime));
  auto keepAliveOffset = builder.CreateStruct(keepAlive);

  auto msg = Gateway::CreateDeviceToGatewayMessage(builder, Gateway::DeviceToGatewayMessagePayload::KeepAlive, keepAliveOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeOtaInstallStartedMessage(const OpenShock::SemVer& semver, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto semVerOffset = Types::CreateSemVerDirect(builder, semver.major, semver.minor, semver.patch, semver.prerelease.data(), semver.build.data());

  auto otaInstallStartedOffset = Gateway::CreateOtaInstallStarted(builder, semVerOffset);

  auto msg = Gateway::CreateDeviceToGatewayMessage(builder, Gateway::DeviceToGatewayMessagePayload::OtaInstallStarted, otaInstallStartedOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeOtaInstallProgressMessage(Gateway::OtaInstallProgressTask task, float progress, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto otaInstallProgressOffset = Gateway::CreateOtaInstallProgress(builder, task, progress);

  auto msg = Gateway::CreateDeviceToGatewayMessage(builder, Gateway::DeviceToGatewayMessagePayload::OtaInstallProgress, otaInstallProgressOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Gateway::SerializeOtaInstallFailedMessage(StringView message, bool fatal, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto messageOffset = builder.CreateString(message.data(), message.size());

  auto otaInstallFailedOffset = Gateway::CreateOtaInstallFailed(builder, messageOffset, fatal);

  auto msg = Gateway::CreateDeviceToGatewayMessage(builder, Gateway::DeviceToGatewayMessagePayload::OtaInstallFailed, otaInstallFailedOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}
