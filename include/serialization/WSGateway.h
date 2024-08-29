#pragma once

#include "FirmwareBootType.h"
#include "SemVer.h"
#include "serialization/CallbackFn.h"
#include "StringView.h"

#include "serialization/_fbs/HubToGatewayMessage_generated.h"

namespace OpenShock::Serialization::Gateway {
  bool SerializeKeepAliveMessage(Common::SerializationCallbackFn callback);
  bool SerializeBootStatusMessage(int32_t otaUpdateId, OpenShock::FirmwareBootType bootType, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallStartedMessage(int32_t updateId, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallProgressMessage(int32_t updateId, Gateway::OtaInstallProgressTask task, float progress, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallFailedMessage(int32_t updateId, StringView message, bool fatal, Common::SerializationCallbackFn callback);
}  // namespace OpenShock::Serialization::Gateway
