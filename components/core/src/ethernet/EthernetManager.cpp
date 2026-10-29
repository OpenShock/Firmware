#include "ethernet/EthernetManager.h"

#include <sdkconfig.h>

// Compiled in where the build enables the internal EMAC (boards whose fragment sets CONFIG_ETH_USE_ESP32_EMAC); which
// PHY is wired up, and on which pins, comes from the board descriptor at runtime.
#if CONFIG_ETH_USE_ESP32_EMAC

const char* const TAG = "EthernetManager";

#include "BoardDescriptor.h"
#include "config/Config.h"
#include "Logging.h"

#include <driver/gpio.h>
#include <esp_eth.h>
#include <esp_eth_mac_esp.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdio>
#include <string>

using namespace OpenShock;

// The LAN8720 on an Olimex ESP32-PoE-style layout has no oscillator of its own; the ESP32 generates the
// 50 MHz RMII clock on GPIO17. Make this per-board when more Ethernet variants land.
static constexpr int kRmiiClockOutGpio = 17;

static esp_netif_t* s_netif       = nullptr;
static esp_eth_handle_t s_eth     = nullptr;
static std::atomic<bool> s_linkUp = false;
static std::atomic<bool> s_hasIp  = false;

static void handleEthEvent(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)base;

  switch (id) {
    case ETHERNET_EVENT_START:
      OS_LOGI(TAG, "ETH start");
      break;
    case ETHERNET_EVENT_CONNECTED: {
      esp_eth_handle_t handle = *static_cast<esp_eth_handle_t*>(data);
      eth_speed_t speed       = ETH_SPEED_10M;
      eth_duplex_t duplex     = ETH_DUPLEX_HALF;
      esp_eth_ioctl(handle, ETH_CMD_G_SPEED, &speed);
      esp_eth_ioctl(handle, ETH_CMD_G_DUPLEX_MODE, &duplex);
      OS_LOGI(TAG, "ETH link up: %s Mbps, %s-duplex", speed == ETH_SPEED_100M ? "100" : "10", duplex == ETH_DUPLEX_FULL ? "full" : "half");
      s_linkUp.store(true, std::memory_order_relaxed);
      break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
      OS_LOGW(TAG, "ETH link down");
      s_linkUp.store(false, std::memory_order_relaxed);
      s_hasIp.store(false, std::memory_order_relaxed);
      break;
    case ETHERNET_EVENT_STOP:
      OS_LOGI(TAG, "ETH stopped");
      s_linkUp.store(false, std::memory_order_relaxed);
      s_hasIp.store(false, std::memory_order_relaxed);
      break;
    default:
      break;
  }
}

static void handleIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)base;

  switch (id) {
    case IP_EVENT_ETH_GOT_IP: {
      const auto* event = static_cast<ip_event_got_ip_t*>(data);
      OS_LOGI(TAG, "ETH got IP: " IPSTR "  netmask=" IPSTR "  gw=" IPSTR, IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
      s_hasIp.store(true, std::memory_order_relaxed);
      break;
    }
    case IP_EVENT_ETH_LOST_IP:
      OS_LOGW(TAG, "ETH lost IP");
      s_hasIp.store(false, std::memory_order_relaxed);
      break;
    default:
      break;
  }
}

static bool powerOnPhy(int8_t powerGpio)
{
  if (powerGpio < 0) {
    return true;
  }

  gpio_num_t pin = static_cast<gpio_num_t>(powerGpio);

  esp_err_t err = gpio_reset_pin(pin);
  if (err == ESP_OK) err = gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  if (err == ESP_OK) err = gpio_set_level(pin, 1);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to power on PHY (GPIO %d): %s", powerGpio, esp_err_to_name(err));
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(10));  // Let the PHY settle before talking SMI
  return true;
}

bool EthernetManager::Init()
{
  if (s_eth != nullptr) {
    return true;
  }

  const BoardDescriptor& board = Board::Get();
  if (board.ethPhy == OPENSHOCK_BOARD_ETH_PHY_NONE) {
    return true;  // No Ethernet on this board
  }
  if (board.ethPhy != OPENSHOCK_BOARD_ETH_PHY_LAN8720) {
    OS_LOGE(TAG, "Unsupported Ethernet PHY type %u in the board descriptor", board.ethPhy);
    return false;
  }
  if (board.ethMdcGpio < 0 || board.ethMdioGpio < 0) {
    OS_LOGE(TAG, "The board descriptor has no MDC/MDIO pins for its Ethernet PHY");
    return false;
  }

  OS_LOGI(TAG, "Initializing Ethernet: addr=%d mdc=%d mdio=%d power=%d clk_out=%d", board.ethPhyAddr, board.ethMdcGpio, board.ethMdioGpio, board.ethPowerGpio, kRmiiClockOutGpio);

  esp_err_t err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, handleEthEvent, nullptr);
  if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, handleIpEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handlers: %s", esp_err_to_name(err));
    return false;
  }

  if (!powerOnPhy(board.ethPowerGpio)) {
    return false;
  }

  eth_mac_config_t macConfig              = ETH_MAC_DEFAULT_CONFIG();
  eth_esp32_emac_config_t emacConfig      = ETH_ESP32_EMAC_DEFAULT_CONFIG();
  emacConfig.smi_gpio.mdc_num             = board.ethMdcGpio;
  emacConfig.smi_gpio.mdio_num            = board.ethMdioGpio;
  emacConfig.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
  emacConfig.clock_config.rmii.clock_gpio = kRmiiClockOutGpio;

  // IDF no longer ships a LAN87xx-specific driver; the LAN8720 is IEEE 802.3 compliant, so the generic PHY drives it.
  eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
  phyConfig.phy_addr         = board.ethPhyAddr;
  phyConfig.reset_gpio_num   = -1;  // No dedicated reset line; the PHY is reset by its power-enable pin

  esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&emacConfig, &macConfig);
  esp_eth_phy_t* phy = esp_eth_phy_new_generic(&phyConfig);
  if (mac == nullptr || phy == nullptr) {
    OS_LOGE(TAG, "Failed to create Ethernet MAC/PHY");
    if (mac != nullptr) mac->del(mac);
    if (phy != nullptr) phy->del(phy);
    return false;
  }

  esp_eth_config_t ethConfig = ETH_DEFAULT_CONFIG(mac, phy);
  err                        = esp_eth_driver_install(&ethConfig, &s_eth);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_eth_driver_install failed: %s", esp_err_to_name(err));
    mac->del(mac);
    phy->del(phy);
    s_eth = nullptr;
    return false;
  }

  esp_netif_config_t netifConfig = ESP_NETIF_DEFAULT_ETH();
  s_netif                        = esp_netif_new(&netifConfig);
  if (s_netif == nullptr) {
    OS_LOGE(TAG, "Failed to create Ethernet netif");
    return false;
  }

  // Same hostname as the WiFi station, so the hub answers to one name on either interface.
  std::string hostname = Config::GetWiFiHostname().value_or(CONFIG_OPENSHOCK_FW_HOSTNAME);
  esp_netif_set_hostname(s_netif, hostname.c_str());

  err = esp_netif_attach(s_netif, esp_eth_new_netif_glue(s_eth));
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to attach Ethernet netif: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_eth_start(s_eth);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_eth_start failed: %s", esp_err_to_name(err));
    return false;
  }

  OS_LOGI(TAG, "Ethernet started; waiting for link and DHCP");
  return true;
}

bool EthernetManager::IsLinkUp()
{
  return s_linkUp.load(std::memory_order_relaxed);
}

bool EthernetManager::HasIPAddress()
{
  return s_hasIp.load(std::memory_order_relaxed);
}

bool EthernetManager::GetIPv4(char* out, std::size_t len)
{
  if (out == nullptr || len == 0 || s_netif == nullptr || !s_hasIp.load(std::memory_order_relaxed)) {
    return false;
  }

  esp_netif_ip_info_t info;
  if (esp_netif_get_ip_info(s_netif, &info) != ESP_OK) {
    return false;
  }

  int n = std::snprintf(out, len, IPSTR, IP2STR(&info.ip));
  return n > 0 && static_cast<std::size_t>(n) < len;
}

#else  // !CONFIG_ETH_USE_ESP32_EMAC

#include "BoardDescriptor.h"

const char* const TAG = "EthernetManager";

#include "Logging.h"

bool OpenShock::EthernetManager::Init()
{
  if (OpenShock::Board::Get().ethPhy != OPENSHOCK_BOARD_ETH_PHY_NONE) {
    OS_LOGE(TAG, "The board has an Ethernet PHY, but this build has no EMAC driver (CONFIG_ETH_USE_ESP32_EMAC)");
    return false;
  }
  return true;  // No Ethernet on this board
}

bool OpenShock::EthernetManager::IsLinkUp()
{
  return false;
}

bool OpenShock::EthernetManager::HasIPAddress()
{
  return false;
}

bool OpenShock::EthernetManager::GetIPv4(char* out, std::size_t len)
{
  (void)out;
  (void)len;
  return false;
}

#endif  // CONFIG_ETH_USE_ESP32_EMAC
