#include <WiFiType.h>
#include <EStopManager.h>
namespace LedManager
{
    void Loop(wl_status_t wifiStatus, bool webSocketConnected, EStopManager::EStopStatus_t EStopStatus, unsigned long millis);
}