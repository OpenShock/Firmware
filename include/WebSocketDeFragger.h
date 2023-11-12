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

    WebSocketDeFragger(EventCallback callback) : m_messages(), m_callback(callback) {}
    WebSocketDeFragger(const WebSocketDeFragger&) = delete;
    ~WebSocketDeFragger() {
      clear();
    }

    void handler(std::uint8_t socketId, WStype_t type, const std::uint8_t* payload, std::size_t length) {
      switch (type) {
      case WStype_FRAGMENT_BIN_START:
        start(socketId, WebSocketMessageType::Binary, payload, length);
        return;
      case WStype_FRAGMENT_TEXT_START:
        start(socketId, WebSocketMessageType::Text, payload, length);
        return;
      case WStype_FRAGMENT:
        append(socketId, payload, length);
        return;
      case WStype_FRAGMENT_FIN:
        finish(socketId, payload, length);
        return;
      [[likely]] default:
        clear(socketId);
        break;
      }

      WebSocketMessageType messageType;
      switch (type) {
      [[unlikely]] case WStype_ERROR:
        messageType = WebSocketMessageType::Error;
        break;
      case WStype_DISCONNECTED:
        messageType = WebSocketMessageType::Disconnected;
        break;
      case WStype_CONNECTED:
        messageType = WebSocketMessageType::Connected;
        break;
      case WStype_TEXT:
        messageType = WebSocketMessageType::Text;
        break;
      [[likely]] case WStype_BIN:
        messageType = WebSocketMessageType::Binary;
        break;
      case WStype_PING:
        messageType = WebSocketMessageType::Ping;
        break;
      case WStype_PONG:
        messageType = WebSocketMessageType::Pong;
        break;
      [[unlikely]] default:
        const char* const errorMessage = "Unknown WebSocket event type";
        m_callback(socketId, WebSocketMessageType::Error, reinterpret_cast<const std::uint8_t*>(errorMessage), strlen(errorMessage));
        return;
      }

      m_callback(socketId, messageType, payload, length);
    }

    void onEvent(const EventCallback& callback) {
      m_callback = callback;
    }
    void clear(std::uint8_t socketId) {
      auto it = m_messages.find(socketId);
      if (it != m_messages.end()) {
        free(it->second.data);
        m_messages.erase(it);
      }
    }
    void clear() {
      for (auto it = m_messages.begin(); it != m_messages.end(); ++it) {
        free(it->second.data);
      }
      m_messages.clear();
    }

    WebSocketDeFragger& operator=(const WebSocketDeFragger&) = delete;
  private:
    void start(std::uint8_t socketId, WebSocketMessageType type, const std::uint8_t* data, std::uint32_t length) {
      auto it = m_messages.find(socketId);
      if (it != m_messages.end()) {
        auto& message = it->second;

        if (message.capacity < length) {
          message.data = reinterpret_cast<std::uint8_t*>(realloc(message.data, length));
          message.capacity = length;
        }

        memcpy(message.data, data, length);
        message.size = length;

        return;
      }

      Message message {
        .data = reinterpret_cast<std::uint8_t*>(malloc(length)),
        .size = length,
        .capacity = length,
        .type = type
      };

      memcpy(message.data, data, length);

      m_messages.insert(std::make_pair(socketId, message));
    }
    void append(std::uint8_t socketId, const std::uint8_t* data, std::uint32_t length) {
      auto it = m_messages.find(socketId);
      if (it == m_messages.end()) {
        return;
      }

      auto& message = it->second;

      std::uint32_t newLength = message.size + length;
      if (message.capacity < newLength) {
        message.data = reinterpret_cast<std::uint8_t*>(realloc(message.data, newLength));
        message.capacity = newLength;
      }

      memcpy(message.data + message.size, data, length);
      message.size = newLength;
    }
    void finish(std::uint8_t socketId, const std::uint8_t* data, std::uint32_t length) {
      auto it = m_messages.find(socketId);
      if (it == m_messages.end()) {
        return;
      }

      auto& message = it->second;

      std::uint32_t newLength = message.size + length;
      if (message.capacity < newLength) {
        message.data = reinterpret_cast<std::uint8_t*>(realloc(message.data, newLength));
        message.capacity = newLength;
      }

      memcpy(message.data + message.size, data, length);
      message.size = newLength;

      m_callback(socketId, message.type, message.data, message.size);

      free(message.data);
      m_messages.erase(it);
    }

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
