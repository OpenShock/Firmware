#pragma once

#include "SemVer.h"
#include "StringView.h"
#include "serialization/CallbackFn.h"

namespace OpenShock::Serialization::Gateway {
  bool SerializeKeepAliveMessage(Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallStartedMessage(SemVer semver, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallProgressMessage(float progress, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallFailedMessage(StringView message, Common::SerializationCallbackFn callback);
  bool SerializeOtaInstallSucceededMessage(SemVer semver, Common::SerializationCallbackFn callback);
}
