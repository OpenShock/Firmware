#include "RetainedRing.h"

#include <algorithm>
#include <cstring>

using namespace OpenShock::Diagnostics;

static constexpr uint32_t kMagic = 0x474E5252;  // "RRNG"

RetainedRing::RetainedRing(RetainedRingState& state, char* data, uint32_t capacity) noexcept
  : m_state(state)
  , m_data(data)
  , m_capacity(capacity)
{
}

uint32_t RetainedRing::computeCheck() const
{
  uint32_t check = ~m_state.magic ^ m_state.capacity;
  check ^= (static_cast<uint32_t>(m_state.written) ^ static_cast<uint32_t>(m_state.written >> 32)) * 0x9E3779B1u;
  check ^= (static_cast<uint32_t>(m_state.consumed) ^ static_cast<uint32_t>(m_state.consumed >> 32)) * 0x85EBCA77u;
  return check;
}

void RetainedRing::seal()
{
  m_state.check = computeCheck();
}

bool RetainedRing::Adopt()
{
  bool intact = m_state.magic == kMagic && m_state.capacity == m_capacity && m_state.consumed <= m_state.written && m_state.check == computeCheck();
  if (!intact) {
    Reset();
  }
  return intact;
}

void RetainedRing::Reset()
{
  m_state.magic    = kMagic;
  m_state.capacity = m_capacity;
  m_state.written  = 0;
  m_state.consumed = 0;
  seal();
}

void RetainedRing::Append(const char* data, std::size_t len)
{
  if (len > m_capacity) {
    // Only the newest `capacity` bytes can survive anyway
    m_state.written += len - m_capacity;
    data += len - m_capacity;
    len = m_capacity;
  }

  std::size_t pos   = static_cast<std::size_t>(m_state.written % m_capacity);
  std::size_t first = std::min<std::size_t>(len, m_capacity - pos);
  std::memcpy(m_data + pos, data, first);
  std::memcpy(m_data, data + first, len - first);

  m_state.written += len;
  seal();
}

std::size_t RetainedRing::Peek(uint64_t start, char* out, std::size_t outCapacity, uint64_t& from) const
{
  uint64_t oldest = m_state.written > m_capacity ? m_state.written - m_capacity : 0;
  from            = std::min(std::max(start, oldest), m_state.written);

  std::size_t len   = static_cast<std::size_t>(std::min<uint64_t>(m_state.written - from, outCapacity));
  std::size_t pos   = static_cast<std::size_t>(from % m_capacity);
  std::size_t first = std::min<std::size_t>(len, m_capacity - pos);
  std::memcpy(out, m_data + pos, first);
  std::memcpy(out + first, m_data, len - first);

  return len;
}

void RetainedRing::Consume(uint64_t upTo)
{
  m_state.consumed = std::clamp(upTo, m_state.consumed, m_state.written);
  seal();
}
