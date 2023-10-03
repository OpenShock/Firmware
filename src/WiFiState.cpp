#include "WiFiState.h"

#include "VisualStateManager.h"

static OpenShock::WiFiState s_wifiState = OpenShock::WiFiState::Disconnected;

OpenShock::WiFiState OpenShock::GetWiFiState() noexcept {
  return s_wifiState;
}
void OpenShock::SetWiFiState(WiFiState state) noexcept {
  if (s_wifiState == state) return;

  s_wifiState = state;
  VisualStateManager::SetWiFiState(state);
}
