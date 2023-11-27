#pragma once

#include "event_handlers/WebSocket.h"
#include "event_handlers/WiFiScan.h"

namespace OpenShock::EventHandlers {
  void Init() {
    WiFiScan::Init();
  }

  void Deinit() {
    WiFiScan::Deinit();
  }
}  // namespace OpenShock::EventHandlers
