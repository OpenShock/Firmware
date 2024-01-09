#pragma once

#include "FirmwareBootType.h"
#include "SemVer.h"
#include "serialization/CallbackFn.h"
#include "StringView.h"

#include "serialization/_fbs/DeviceToGatewayMessage_generated.h"

namespace OpenShock::Serialization::Gateway {
  bool SerializeKeepAliveMessage(Common::SerializationCallbackFn callback);
  bool SerializeBootStatusMessage(std::int32_t otaUpdateId, OpenShock::FirmwareBootType bootType, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallStartedMessage(std::int32_t updateId, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallProgressMessage(std::int32_t updateId, Gateway::OtaInstallProgressTask task, float progress, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallFailedMessage(std::int32_t updateId, StringView message, bool fatal, Common::SerializationCallbackFn callback);
}  // namespace OpenShock::Serialization::Gateway
