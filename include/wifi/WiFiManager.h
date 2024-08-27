#pragma once

#include "wifi/WiFiNetwork.h"
#include "StringView.h"

#include <cstdint>
#include <string>
#include <vector>

namespace OpenShock::WiFiManager {
  /// @brief Initializes the WiFiManager
  /// @return True if the WiFiManager was initialized successfully
  bool Init();

  /// @brief Saves a network to the config
  /// @param ssid SSID of the network
  /// @param password Password of the network
  /// @return True if the network was saved successfully
  bool Save(const char* ssid, StringView password);

  /// @brief Removes a network from the config by it's SSID
  /// @param ssid SSID of the network
  /// @return True if the network was removed successfully
  bool Forget(const char* ssid);

  /// @brief Refreshes all the networks with updated credential IDs from the config
  /// @return True if the networks were refreshed successfully
  bool RefreshNetworkCredentials();

  /// @brief Checks if a network is saved in the config by it's SSID
  /// @param ssid SSID of the network
  /// @return True if a saved network matches the given SSID
  bool IsSaved(const char* ssid);

  /// @brief Connects to a saved network by it's SSID
  /// @param ssid SSID of the network
  /// @return True if the saved network was found and the connection process was started successfully
  bool Connect(const char* ssid);

  /// @brief Connects to a saved network by it's BSSID
  /// @param bssid BSSID of the network
  /// @return True if the saved network was found and the connection process was started successfully
  bool Connect(const std::uint8_t (&bssid)[6]);

  /// @brief Disconnects from the currently connected network
  void Disconnect();

  /// @brief Checks if the device is currently connected to a network
  /// @return True if the device is connected to a network
  bool IsConnected();

  /// @brief Gets the currently connected network
  /// @param network Variable to store the network info in
  /// @return True if the device is connected to a network
  bool GetConnectedNetwork(OpenShock::WiFiNetwork& network);

  /// @brief Gets the devices IP address if it's connected to a network (IPv4)
  /// @param ipAddress Variable to store the IP address in
  /// @return True if the device is connected to a network
  bool GetIPAddress(char* ipAddress);

  /// @brief Gets the devices IP address if it's connected to a network (IPv6)
  /// @param ipAddress Variable to store the IP address in
  /// @return True if the device is connected to a network
  bool GetIPv6Address(char* ipAddress);

  /// @brief Runs the WiFiManager loop
  void Update();

  /// @brief Gets a copy of the vector of discovered WiFi networks
  /// @return Vector of discovered WiFiNetworks
  std::vector<WiFiNetwork> GetDiscoveredWiFiNetworks();
}  // namespace OpenShock::WiFiManager
