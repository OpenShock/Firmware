#pragma once

namespace OpenShock {
  enum class WiFiState {
    Disconnected,
    Scanning,
    Connecting,
    Connected
  };

  WiFiState GetWiFiState() noexcept;
  void SetWiFiState(WiFiState state) noexcept;
}  // namespace OpenShock
