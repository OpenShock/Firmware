#pragma once

#include "WebSocketMessageType.h"

#include <WebSockets.h>

#include <map>
#include <cstdint>
#include <functional>

namespace OpenShock {
  class WebSocketDeFragger {
  public:

    typedef std::function<void(std::uint8_t socketId, WebSocketMessageType type, const std::uint8_t* data, std::uint32_t length)> EventCallback;

    WebSocketDeFragger(EventCallback callback);
    WebSocketDeFragger(const WebSocketDeFragger&) = delete;
    ~WebSocketDeFragger();

    void handler(std::uint8_t socketId, WStype_t type, const std::uint8_t* payload, std::size_t length);
    void onEvent(const EventCallback& callback);
    void clear(std::uint8_t socketId);
    void clear();

    WebSocketDeFragger& operator=(const WebSocketDeFragger&) = delete;
  private:
    void start(std::uint8_t socketId, WebSocketMessageType type, const std::uint8_t* data, std::uint32_t length);
    void append(std::uint8_t socketId, const std::uint8_t* data, std::uint32_t length);
    void finish(std::uint8_t socketId, const std::uint8_t* data, std::uint32_t length);

    struct Message {
      std::uint8_t* data;
      std::uint32_t size;
      std::uint32_t capacity;
      WebSocketMessageType type;
    };

    std::map<std::uint8_t, Message> m_messages;
    EventCallback m_callback;
  };
}
