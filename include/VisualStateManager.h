#pragma once

#include <cstdint>

namespace ShockLink::VisualStateManager
{
    enum class ConnectionState {
        WiFi_Error,
        WiFi_NoConnection,
        Ping_NoResponse,
        WebSocket_CantConnect,
        WebSocket_Connected,
    };

    void SetCriticalError(bool criticalError);
    void SetConnectionState(ConnectionState state);
} // namespace ShockLink::VisualStateManager
