#pragma once

#include <WebSockets.h>

#include <cstdint>

class String;
class WebSocketsClient;

namespace ShockLink {
  class APIConnection {
  public:
    APIConnection(const String& authToken);
    ~APIConnection();

    void Update();

  private:
    void parseMessage(char* data, std::size_t length);
    void handleEvent(WStype_t type, std::uint8_t* payload, std::size_t length);
    void sendKeepAlive();

    WebSocketsClient* m_webSocket;
  };
}  // namespace ShockLink
