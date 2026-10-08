#include "network/NetworkManager.h"

const char* const TAG = "NetworkManager";

#include "events/Events.h"
#include "Logging.h"
#include "SimpleMutex.h"

#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>

#include <atomic>

using namespace OpenShock;

static OpenShock::SimpleMutex s_mutex = {};  // Serializes state transitions so UP/DOWN edges are posted once
static std::atomic<bool> s_wifiHasIp  = false;
static std::atomic<bool> s_ethHasIp   = false;

static NetworkManager::Interface computePreferred()
{
  if (s_ethHasIp.load(std::memory_order_relaxed)) return NetworkManager::Interface::Ethernet;
  if (s_wifiHasIp.load(std::memory_order_relaxed)) return NetworkManager::Interface::WiFi;
  return NetworkManager::Interface::None;
}

[[maybe_unused]] static const char* ifaceName(NetworkManager::Interface iface)  // Only used in logs, which may be compiled out
{
  switch (iface) {
    case NetworkManager::Interface::WiFi:
      return "WiFi";
    case NetworkManager::Interface::Ethernet:
      return "Ethernet";
    default:
      return "None";
  }
}

static void postNetworkEvent(int32_t eventId, NetworkManager::Interface iface)
{
  openshock_network_event_t payload = {static_cast<uint8_t>(iface)};

  esp_err_t err = esp_event_post(OPENSHOCK_EVENTS, eventId, &payload, sizeof(payload), portMAX_DELAY);
  if (err != ESP_OK) {
    OS_LOGW(TAG, "Failed to post network event %ld: %s", static_cast<long>(eventId), esp_err_to_name(err));
  }
}

// Route outgoing traffic through the preferred interface.
static void setDefaultNetif(NetworkManager::Interface iface)
{
  const char* key = nullptr;
  switch (iface) {
    case NetworkManager::Interface::WiFi:
      key = "WIFI_STA_DEF";
      break;
    case NetworkManager::Interface::Ethernet:
      key = "ETH_DEF";
      break;
    default:
      return;
  }

  esp_netif_t* netif = esp_netif_get_handle_from_ifkey(key);
  if (netif == nullptr) {
    OS_LOGW(TAG, "No netif handle for %s", key);
    return;
  }

  esp_err_t err = esp_netif_set_default_netif(netif);
  if (err != ESP_OK) {
    OS_LOGW(TAG, "Failed to set default netif to %s: %s", key, esp_err_to_name(err));
    return;
  }

  OS_LOGI(TAG, "Default netif set to %s", key);
}

static void onInterfaceIpChanged(NetworkManager::Interface iface, bool hasIp)
{
  OpenShock::ScopedLock lock__(&s_mutex);

  bool hadAny                             = NetworkManager::HasIP();
  NetworkManager::Interface prevPreferred = computePreferred();

  if (iface == NetworkManager::Interface::WiFi) {
    s_wifiHasIp.store(hasIp, std::memory_order_relaxed);
  } else if (iface == NetworkManager::Interface::Ethernet) {
    s_ethHasIp.store(hasIp, std::memory_order_relaxed);
  } else {
    return;
  }

  bool hasAny                            = NetworkManager::HasIP();
  NetworkManager::Interface newPreferred = computePreferred();

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
  } else if (hasAny && (hasIp || newPreferred != prevPreferred)) {
    OS_LOGI(TAG, "Network GOT_IP via %s (preferred was %s)", ifaceName(newPreferred), ifaceName(prevPreferred));
    postNetworkEvent(OPENSHOCK_EVENT_NETWORK_GOT_IP, newPreferred);
  }
}

// WiFi connectivity comes from WiFiManager's coarse state, which already folds in STA disconnects and lost IPs.
static void handleWiFiStateChanged(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)base;
  (void)id;

  switch (*static_cast<OpenShockWiFiState*>(data)) {
    case OPENSHOCK_WIFI_STATE_GOT_IP:
      onInterfaceIpChanged(NetworkManager::Interface::WiFi, true);
      break;
    case OPENSHOCK_WIFI_STATE_DISCONNECTED:
      onInterfaceIpChanged(NetworkManager::Interface::WiFi, false);
      break;
    default:
      break;
  }
}

static void handleIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)base;
  (void)data;

  switch (id) {
    case IP_EVENT_ETH_GOT_IP:
      onInterfaceIpChanged(NetworkManager::Interface::Ethernet, true);
      break;
    case IP_EVENT_ETH_LOST_IP:
      onInterfaceIpChanged(NetworkManager::Interface::Ethernet, false);
      break;
    default:
      break;
  }
}

static void handleEthEvent(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)base;
  (void)data;

  // LOST_IP isn't emitted until the DHCP lease times out; drop the IP ourselves on link loss for faster failover.
  if (id == ETHERNET_EVENT_DISCONNECTED) {
    onInterfaceIpChanged(NetworkManager::Interface::Ethernet, false);
  }
}

bool NetworkManager::Init()
{
  esp_err_t err;

  err = esp_event_handler_register(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_WIFI_STATE_CHANGED, handleWiFiStateChanged, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register WiFi state handler: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, handleIpEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, handleEthEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register ETH_EVENT handler: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

NetworkManager::Interface NetworkManager::GetActive()
{
  return computePreferred();
}

bool NetworkManager::HasIP()
{
  return s_wifiHasIp.load(std::memory_order_relaxed) || s_ethHasIp.load(std::memory_order_relaxed);
}

bool NetworkManager::IsWiFiConnected()
{
  return s_wifiHasIp.load(std::memory_order_relaxed);
}

bool NetworkManager::IsEthernetConnected()
{
  return s_ethHasIp.load(std::memory_order_relaxed);
}
