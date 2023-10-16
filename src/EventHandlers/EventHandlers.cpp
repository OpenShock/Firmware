#include "EventHandlers/EventHandlers.h"

#include "EventHandlers/WiFiScanEventHandlers.h"

void OpenShock::EventHandlers::Init() {
  OpenShock::EventHandlers::WiFiScanEventHandler::Init();
}

void OpenShock::EventHandlers::Deinit() {
  OpenShock::EventHandlers::WiFiScanEventHandler::Deinit();
}
