#include "network/NetworkManager.h"

const char* const TAG = "NetworkManager";

#include "events/Events.h"
#include "Logging.h"

#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_netif_types.h>
#include <esp_wifi.h>
#include <lwip/netif.h>

#include <atomic>

using namespace OpenShock;

static std::atomic<bool> s_wifiHasIp {false};
static std::atomic<bool> s_ethHasIp {false};

static NetworkManager::Interface computePreferred()
{
  if (s_ethHasIp.load()) return NetworkManager::Interface::Ethernet;
  if (s_wifiHasIp.load()) return NetworkManager::Interface::WiFi;
  return NetworkManager::Interface::None;
}

static const char* ifaceName(NetworkManager::Interface iface)
{
  switch (iface) {
    case NetworkManager::Interface::WiFi:     return "WiFi";
    case NetworkManager::Interface::Ethernet: return "Ethernet";
    default:                                  return "None";
  }
}

static void postNetworkEvent(int32_t event_id, NetworkManager::Interface iface)
{
  openshock_network_event_t payload = {static_cast<uint8_t>(iface)};
  esp_err_t err                     = esp_event_post(OPENSHOCK_EVENTS, event_id, &payload, sizeof(payload), portMAX_DELAY);
  if (err != ESP_OK) {
    OS_LOGW(TAG, "Failed to post network event %ld: %s", static_cast<long>(event_id), esp_err_to_name(err));
  }
}

static void setDefaultNetif(NetworkManager::Interface iface)
{
  const char* key = nullptr;
  switch (iface) {
    case NetworkManager::Interface::WiFi:     key = "WIFI_STA_DEF"; break;
    case NetworkManager::Interface::Ethernet: key = "ETH_DEF"; break;
    default:                                  return;
  }

  esp_netif_t* espNetif = esp_netif_get_handle_from_ifkey(key);
  if (espNetif == nullptr) {
    OS_LOGW(TAG, "No netif handle for %s", key);
    return;
  }

  // IDF 4.4 has no esp_netif_set_default_netif; drop down to lwIP directly.
  auto* lwipNetif = static_cast<struct netif*>(esp_netif_get_netif_impl(espNetif));
  if (lwipNetif == nullptr) {
    OS_LOGW(TAG, "No lwIP netif backing %s", key);
    return;
  }

  netif_set_default(lwipNetif);
  OS_LOGI(TAG, "Default netif set to %s", key);
}

static void onInterfaceIpChanged(NetworkManager::Interface iface, bool hasIp)
{
  bool hadAny = s_wifiHasIp.load() || s_ethHasIp.load();
  NetworkManager::Interface prevPreferred = computePreferred();

  if (iface == NetworkManager::Interface::WiFi) {
    s_wifiHasIp.store(hasIp);
  } else if (iface == NetworkManager::Interface::Ethernet) {
    s_ethHasIp.store(hasIp);
  } else {
    return;
  }

  bool hasAny                             = s_wifiHasIp.load() || s_ethHasIp.load();
  NetworkManager::Interface newPreferred  = computePreferred();

  if (hasAny) {
    setDefaultNetif(newPreferred);
  }

  if (!hadAny && hasAny) {
    OS_LOGI(TAG, "Network UP via %s", ifaceName(newPreferred));
    postNetworkEvent(OPENSHOCK_EVENT_NETWORK_UP, newPreferred);
    postNetworkEvent(OPENSHOCK_EVENT_NETWORK_GOT_IP, newPreferred);
  } else if (hadAny && !hasAny) {
    OS_LOGW(TAG, "Network DOWN");
    postNetworkEvent(OPENSHOCK_EVENT_NETWORK_DOWN, NetworkManager::Interface::None);
  } else if (hasIp || newPreferred != prevPreferred) {
    OS_LOGI(TAG, "Network GOT_IP via %s (preferred was %s)", ifaceName(newPreferred), ifaceName(prevPreferred));
    postNetworkEvent(OPENSHOCK_EVENT_NETWORK_GOT_IP, newPreferred);
  }
}

static void handleIpEvent(void* /*arg*/, esp_event_base_t /*base*/, int32_t id, void* /*data*/)
{
  switch (id) {
    case IP_EVENT_STA_GOT_IP:  onInterfaceIpChanged(NetworkManager::Interface::WiFi, true); break;
    case IP_EVENT_STA_LOST_IP: onInterfaceIpChanged(NetworkManager::Interface::WiFi, false); break;
    case IP_EVENT_ETH_GOT_IP:  onInterfaceIpChanged(NetworkManager::Interface::Ethernet, true); break;
    case IP_EVENT_ETH_LOST_IP: onInterfaceIpChanged(NetworkManager::Interface::Ethernet, false); break;
    default:                   break;
  }
}

static void handleWifiEvent(void* /*arg*/, esp_event_base_t /*base*/, int32_t id, void* /*data*/)
{
  // LOST_IP on STA disconnect isn't always emitted immediately; drop the IP bit
  // ourselves on explicit disconnect for faster failover.
  if (id == WIFI_EVENT_STA_DISCONNECTED) {
    onInterfaceIpChanged(NetworkManager::Interface::WiFi, false);
  }
}

static void handleEthEvent(void* /*arg*/, esp_event_base_t /*base*/, int32_t id, void* /*data*/)
{
  if (id == ETHERNET_EVENT_DISCONNECTED) {
    onInterfaceIpChanged(NetworkManager::Interface::Ethernet, false);
  }
}

bool NetworkManager::Init()
{
  esp_err_t err;

  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, handleIpEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, handleWifiEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
    return false;
  }

#ifdef OPENSHOCK_ETHERNET_LAN8720
  err = esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, handleEthEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register ETH_EVENT handler: %s", esp_err_to_name(err));
    return false;
  }
#endif

  return true;
}

NetworkManager::Interface NetworkManager::GetActive()
{
  return computePreferred();
}

bool NetworkManager::HasIP()
{
  return s_wifiHasIp.load() || s_ethHasIp.load();
}

bool NetworkManager::IsWiFiConnected()
{
  return s_wifiHasIp.load();
}

bool NetworkManager::IsEthernetConnected()
{
  return s_ethHasIp.load();
}
