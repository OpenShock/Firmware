#include "WebSocketDeFragger.h"

const char* const TAG = "WebSocketDeFragger";

#include "Logging.h"

#include <cstring>

using namespace OpenShock;

WebSocketDeFragger::WebSocketDeFragger(EventCallback callback)
  : m_messages()
  , m_callback(callback)
{
}

WebSocketDeFragger::~WebSocketDeFragger()
{
  clear();
}

void WebSocketDeFragger::handler(uint8_t socketId, WStype_t type, const uint8_t* payload, std::size_t length)
{
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
      OS_LOGE(TAG, "WebSocket client #%u error %u: %s", socketId, length, reinterpret_cast<const char*>(payload));
      return;
  }

  m_callback(socketId, messageType, tcb::span<const uint8_t>(payload, length));
}

void WebSocketDeFragger::onEvent(const EventCallback& callback)
{
  m_callback = callback;
}

void WebSocketDeFragger::clear(uint8_t socketId)
{
  auto it = m_messages.find(socketId);
  if (it != m_messages.end()) {
    it->second.data.clear();
    m_messages.erase(it);
  }
}

void WebSocketDeFragger::clear()
{
  m_messages.clear();
}

void WebSocketDeFragger::start(uint8_t socketId, WebSocketMessageType type, const uint8_t* data, uint32_t length)
{
  auto it = m_messages.find(socketId);
  if (it != m_messages.end()) {
    it->second.data.assign(data, length);
    return;
  }

  m_messages.insert(std::make_pair(socketId, Message {.data = TinyVec<uint8_t>(data, length), .type = type}));
}

void WebSocketDeFragger::append(uint8_t socketId, const uint8_t* data, uint32_t length)
{
  auto it = m_messages.find(socketId);
  if (it == m_messages.end()) {
    return;
  }

  it->second.data.append(data, length);
}

void WebSocketDeFragger::finish(uint8_t socketId, const uint8_t* data, uint32_t length)
{
  auto it = m_messages.find(socketId);
  if (it == m_messages.end()) {
    return;
  }

  auto& message = it->second;
  message.data.append(data, length);

  m_callback(socketId, message.type, message.data);

  message.data.clear();
  m_messages.erase(it);
}
