#include "message_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "util/HexUtils.h"
#include "wifi/WiFiManager.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWiFiNetworkDisconnectCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root)
{
  (void)socketId;

  auto msg = root->payload_as_WifiNetworkDisconnectCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as WiFiNetworkDisconnectCommand");
    return;
  }

  WiFiManager::Disconnect();
}
