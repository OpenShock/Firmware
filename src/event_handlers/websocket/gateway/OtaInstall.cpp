#include "event_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "CaptivePortal.h"
#include "Logging.h"
#include "OtaUpdateManager.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleOtaInstall(const OpenShock::Serialization::Gateway::GatewayToHubMessage* root) {
  auto msg = root->payload_as_OtaInstall();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as OtaInstall");
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

  OS_LOGI(TAG, "OTA install requested for version %s", version.toString().c_str());  // TODO: This is abusing the SemVer::toString() method causing alot of string copies, fix this

  if (!OpenShock::OtaUpdateManager::TryStartFirmwareInstallation(version)) {
    OS_LOGE(TAG, "Failed to install firmware");  // TODO: Send error message to server
    return;
  }
}
