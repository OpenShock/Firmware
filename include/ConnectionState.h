#pragma once

namespace ShockLink {
    enum class ConnectionState {
        WiFi_Disconnected,
        WiFi_Connecting,
        WiFi_Connected,
        Ping_NoResponse,
        WebSocket_CantConnect,
        WebSocket_Connected,
    };
}