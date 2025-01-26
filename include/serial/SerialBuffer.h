#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

#include "Common.h"

namespace OpenShock::Serial {
  class SerialBuffer {
    DISABLE_COPY(SerialBuffer);
    DISABLE_MOVE(SerialBuffer);

  public:
    constexpr SerialBuffer()
      : m_data(nullptr)
      , m_size(0)
      , m_capacity(0)
    {
    }
    SerialBuffer(size_t capacity)
      : m_data(new char[capacity])
      , m_size(0)
      , m_capacity(capacity)
    {
    }
    ~SerialBuffer() { delete[] m_data; }

    constexpr char* data() { return m_data; }
    constexpr size_t size() const { return m_size; }
    constexpr size_t capacity() const { return m_capacity; }
    constexpr bool empty() const { return m_size == 0; }

    constexpr void clear() { m_size = 0; }
    void destroy()
    {
      delete[] m_data;
      m_data     = nullptr;
      m_size     = 0;
      m_capacity = 0;
    }

    void reserve(size_t size)
    {
      size = (size + 31) & ~31;  // Align to 32 bytes

      if (size <= m_capacity) {
        return;
      }

      char* newData = new char[size];
      if (m_data != nullptr) {
        memcpy(newData, m_data, m_size);
        delete[] m_data;
      }

      m_data     = newData;
      m_capacity = size;
    }

    void push_back(char c)
    {
      if (m_size >= m_capacity) {
        reserve(m_capacity + 16);
      }

      m_data[m_size++] = c;
    }

    constexpr void pop_back()
    {
      if (m_size > 0) {
        --m_size;
      }
    }

    constexpr operator std::string_view() const { return std::string_view(m_data, m_size); }

  private:
    char* m_data;
    size_t m_size;
    size_t m_capacity;
  };
}  // namespace OpenShock::Serial
