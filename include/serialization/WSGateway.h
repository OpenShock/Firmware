#pragma once

#include "FirmwareBootType.h"
#include "SemVer.h"
#include "serialization/CallbackFn.h"

#include "serialization/_fbs/HubToGatewayMessage_generated.h"

#include <string_view>

#define SERIALIZER_FN(NAME, ...) bool Serialize##NAME##Message(__VA_ARGS__ __VA_OPT__(, ) Common::SerializationCallbackFn callback)

namespace OpenShock::Serialization::Gateway {
  SERIALIZER_FN(Pong);
  SERIALIZER_FN(BootStatus, int32_t updateId, OpenShock::FirmwareBootType bootType);
  SERIALIZER_FN(OtaUpdateStarted, int32_t updateId, const OpenShock::SemVer& version);
  SERIALIZER_FN(OtaUpdateProgress, int32_t updateId, Types::OtaUpdateProgressTask task, float progress);
  SERIALIZER_FN(OtaUpdateFailed, int32_t updateId, std::string_view message, bool fatal);
}  // namespace OpenShock::Serialization::Gateway

#undef SERIALZIER_FN
