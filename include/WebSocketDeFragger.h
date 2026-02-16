#pragma once

#include "Common.h"
#include "TinyVec.h"
#include "WebSocketMessageType.h"

#include <WebSockets.h>

#include <cstdint>
#include <functional>
#include <map>
#include <span>

namespace OpenShock {
  class WebSocketDeFragger {
    DISABLE_COPY(WebSocketDeFragger);
    DISABLE_MOVE(WebSocketDeFragger);

  public:
    typedef std::function<void(uint8_t socketId, WebSocketMessageType type, std::span<const uint8_t> data)> EventCallback;

    WebSocketDeFragger(EventCallback callback);
    ~WebSocketDeFragger();

    void handler(uint8_t socketId, WStype_t type, const uint8_t* payload, std::size_t length);
    void onEvent(const EventCallback& callback);
    void clear(uint8_t socketId);
    void clear();

  private:
    void start(uint8_t socketId, WebSocketMessageType type, const uint8_t* data, uint32_t length);
    void append(uint8_t socketId, const uint8_t* data, uint32_t length);
    void finish(uint8_t socketId, const uint8_t* data, uint32_t length);

    struct Message {
      TinyVec<uint8_t> data;
      WebSocketMessageType type;
    };

    std::map<uint8_t, Message> m_messages;
    EventCallback m_callback;
  };
}  // namespace OpenShock
