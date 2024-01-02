#include "event_handlers/impl/WSGateway.h"

#include "CaptivePortal.h"
#include "Logging.h"
#include "OtaUpdateManager.h"

#include <cstdint>

const char* const TAG = "ServerMessageHandlers";

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleOtaInstall(const OpenShock::Serialization::ServerToDeviceMessage* root) {
  auto msg = root->payload_as_OtaInstall();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as OtaInstall");
    return;
  }

  auto semver = msg->version();
  if (semver == nullptr) {
    ESP_LOGE(TAG, "Version cannot be parsed");
    return;
  }

  char buffer[18];
  if (snprintf(buffer, sizeof(buffer), "%u.%u.%u", semver->major(), semver->minor(), semver->patch()) < 0) {
    ESP_LOGE(TAG, "Failed to format version string");
    return;
  }


  std::string version;
  version.reserve(32);

  version += buffer;

  auto prerelease = semver->prerelease();
  if (prerelease != nullptr) {
    version += "-";
    version += prerelease->c_str();
  }

  auto build = semver->build();
  if (build != nullptr) {
    version += "+";
    version += build->c_str();
  }

  ESP_LOGI(TAG, "OTA install requested for version %s", version.c_str());

  if (!OpenShock::OtaUpdateManager::TryStartFirmwareInstallation(version)) {
    ESP_LOGE(TAG, "Failed to install firmware"); // TODO: Send error message to server
    return;
  }
}
