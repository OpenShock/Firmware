#pragma once

#include <cstdint>
#include <string>

namespace OpenShock::CaptivePortal {
  void SetAlwaysEnabled(bool alwaysEnabled);
  bool IsAlwaysEnabled();

  bool IsRunning();
  void Update();

  bool SendMessageTXT(std::uint8_t socketId, const char* data, std::size_t len);
  bool SendMessageBIN(std::uint8_t socketId, const std::uint8_t* data, std::size_t len);
  inline bool SendMessageTXT(std::uint8_t socketId, const std::string& message) {
    return SendMessageTXT(socketId, message.c_str(), message.length());
  }

  bool BroadcastMessageTXT(const char* data, std::size_t len);
  bool BroadcastMessageBIN(const std::uint8_t* data, std::size_t len);
  inline bool BroadcastMessageTXT(const std::string& message) {
    return BroadcastMessageTXT(message.c_str(), message.length());
  }
}  // namespace OpenShock::CaptivePortal
