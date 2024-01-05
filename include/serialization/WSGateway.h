#pragma once

#include "FirmwareBootType.h"
#include "SemVer.h"
#include "serialization/CallbackFn.h"
#include "StringView.h"

#include "serialization/_fbs/DeviceToGatewayMessage_generated.h"

namespace OpenShock::Serialization::Gateway {
  bool SerializeKeepAliveMessage(Common::SerializationCallbackFn callback);
  bool SerializeBootStatusMessage(OpenShock::FirmwareBootType bootType, const OpenShock::SemVer& version, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallStartedMessage(const OpenShock::SemVer& semver, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallProgressMessage(Gateway::OtaInstallProgressTask task, float progress, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallFailedMessage(StringView message, bool fatal, Common::SerializationCallbackFn callback);
}  // namespace OpenShock::Serialization::Gateway
