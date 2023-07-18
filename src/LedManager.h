#include <WiFiType.h>

namespace LedManager
{
    void Setup();
    void Loop(wl_status_t wifiStatus, bool webSocketConnected, unsigned long millis);
}