#pragma once

#include <ArduinoJson.h>
#include <WString.h>

#include <cstdint>

namespace OpenShock::CaptivePortal {
  bool Start();
  void Stop();
  bool IsRunning();
  void Update();

  bool BroadcastMessageTXT(const char* data, std::size_t len);
  bool BroadcastMessageBIN(const std::uint8_t* data, std::size_t len);
  inline bool BroadcastMessageTXT(const String& message) {
    return BroadcastMessageTXT(message.c_str(), message.length());
  }
  inline bool BroadcastMessageJSON(const DynamicJsonDocument& doc) {
    String message;
    serializeJson(doc, message);
    return BroadcastMessageTXT(message);
  }
};  // namespace OpenShock::CaptivePortal
