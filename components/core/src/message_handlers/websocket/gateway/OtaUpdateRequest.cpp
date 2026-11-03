#include "message_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "AppHooks.h"
#include "Logging.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleOtaUpdateRequest(OpenShock::GatewayClient&, const OpenShock::Serialization::Gateway::GatewayToHubMessage* root)
{
  auto msg = root->payload_as_OtaUpdateRequest();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as OtaUpdate");
    return;
  }

  auto semver = msg->version();
  if (semver == nullptr) {
    OS_LOGE(TAG, "Version cannot be parsed");
    return;
  }

  std::string_view prerelease, build;
  if (semver->prerelease() != nullptr) {
    prerelease = std::string_view(semver->prerelease()->c_str(), semver->prerelease()->size());
  }
  if (semver->build() != nullptr) {
    build = std::string_view(semver->build()->c_str(), semver->build()->size());
  }

  OpenShock::SemVer version(semver->major(), semver->minor(), semver->patch(), prerelease, build);

  OS_LOGI(
    TAG,
    "OTA update requested for version %u.%u.%u%s%.*s%s%.*s",
    version.major,
    version.minor,
    version.patch,
    prerelease.empty() ? "" : "-",
    static_cast<int>(prerelease.size()),
    prerelease.data(),
    build.empty() ? "" : "+",
    static_cast<int>(build.size()),
    build.data()
  );

  if (!OpenShock::AppHooks::OtaTryStartFirmwareUpdate(version)) {
    OS_LOGE(TAG, "Failed to update firmware");  // TODO: Send error message to server
    return;
  }
}
