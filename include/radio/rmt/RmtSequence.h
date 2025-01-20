#pragma once

#include "Common.h"

#include <esp32-hal-rmt.h>

#include <cstdint>

namespace OpenShock::Rmt {
  class RmtSequence {
    DISABLE_COPY(RmtSequence);

  public:
    RmtSequence()
      : m_data(nullptr)
      , m_size(0)
    {
    }
    RmtSequence(size_t size)
      : m_data(reinterpret_cast<rmt_data_t*>(malloc(size * 2 * sizeof(rmt_data_t))))
      , m_size(size)
    {
    }
    RmtSequence(RmtSequence&& other) noexcept
      : m_data(other.m_data)
      , m_size(other.m_size)
    {
      other.m_data = nullptr;
      other.m_size = 0;
    }
    ~RmtSequence() { free(m_data); }

    constexpr bool is_valid() const noexcept { return m_data != nullptr && m_size != 0; }

    constexpr rmt_data_t* payload() noexcept { return m_data; }
    constexpr rmt_data_t* terminator() noexcept { return m_data + m_size; }
    constexpr size_t size() const noexcept { return m_size; }

    RmtSequence& operator=(RmtSequence&& other)
    {
      if (this == &other) return *this;

      free(m_data);

      m_data = other.m_data;
      m_size = other.m_size;

      other.m_data = nullptr;
      other.m_size = 0;

      return *this;
    }

  private:
    rmt_data_t* m_data;
    size_t m_size;
  };
}  // namespace OpenShock::Rmt
