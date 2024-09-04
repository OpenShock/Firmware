#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/CommandEntry.h"
#include "serial/command_handlers/impl/common.h"

#include "FormatHelpers.h"
#include "Time.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiNetwork.h"

void _handleDebugInfoCommand(OpenShock::StringView arg) {
  (void)arg;

  SERPR_RESPONSE("RTOSInfo|Free Heap|%u", xPortGetFreeHeapSize());
  SERPR_RESPONSE("RTOSInfo|Min Free Heap|%u", xPortGetMinimumEverFreeHeapSize());

  const int64_t now = OpenShock::millis();
  SERPR_RESPONSE("RTOSInfo|UptimeMS|%lli", now);

  const int64_t seconds = now / 1000;
  const int64_t minutes = seconds / 60;
  const int64_t hours   = minutes / 60;
  const int64_t days    = hours / 24;
  SERPR_RESPONSE("RTOSInfo|Uptime|%llid %llih %llim %llis", days, hours % 24, minutes % 60, seconds % 60);

  OpenShock::WiFiNetwork network;
  bool connected = OpenShock::WiFiManager::GetConnectedNetwork(network);
  SERPR_RESPONSE("WiFiInfo|Connected|%s", connected ? "true" : "false");
  if (connected) {
    SERPR_RESPONSE("WiFiInfo|SSID|%s", network.ssid);
    SERPR_RESPONSE("WiFiInfo|BSSID|" BSSID_FMT, BSSID_ARG(network.bssid));

    char ipAddressBuffer[64];
    OpenShock::WiFiManager::GetIPAddress(ipAddressBuffer);
    SERPR_RESPONSE("WiFiInfo|IPv4|%s", ipAddressBuffer);
    OpenShock::WiFiManager::GetIPv6Address(ipAddressBuffer);
    SERPR_RESPONSE("WiFiInfo|IPv6|%s", ipAddressBuffer);
  }
}

OpenShock::Serial::CommandHandlers::CommandEntry OpenShock::Serial::CommandHandlers::SysInfoHandler() {
  return OpenShock::Serial::CommandHandlers::CommandEntry("sysinfo"_sv, "Get system information from RTOS, WiFi, etc."_sv, _handleDebugInfoCommand);
}
