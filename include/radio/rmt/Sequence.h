#pragma once

#include "Common.h"
#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <esp32-hal-rmt.h>

#include <cstdint>

namespace OpenShock::Rmt {
  class Sequence {
    DISABLE_COPY(Sequence);

  public:
    Sequence()
      : m_data(nullptr)
      , m_size(0)
      , m_transmitEnd(0)
      , m_shockerId(0)
      , m_shockerModel()
    {
    }
    Sequence(ShockerModelType shockerModel, uint16_t shockerId, int64_t transmitEnd);
    Sequence(Sequence&& other) noexcept
      : m_data(other.m_data)
      , m_size(other.m_size)
      , m_transmitEnd(other.m_transmitEnd)
      , m_shockerId(other.m_shockerId)
      , m_shockerModel(other.m_shockerModel)
    {
      other.reset();
    }
    ~Sequence() { free(m_data); }

    inline bool is_valid() const noexcept { return m_data != nullptr && m_size > 0; }

    inline ShockerModelType shockerModel() const noexcept { return m_shockerModel; }
    inline uint16_t shockerId() const noexcept { return m_shockerId; }

    inline int64_t transmitEnd() const noexcept { return m_transmitEnd; }
    inline void setTransmitEnd(int64_t transmitEnd) noexcept { m_transmitEnd = transmitEnd; }

    inline rmt_data_t* payload() noexcept { return m_data; }
    inline const rmt_data_t* payload() const noexcept { return m_data; }
    inline rmt_data_t* terminator() noexcept { return m_data + m_size; }
    inline const rmt_data_t* terminator() const noexcept { return m_data + m_size; }
    inline size_t size() const noexcept { return m_size; }

    bool fill(ShockerCommandType commandType, uint8_t intensity);

    Sequence& operator=(Sequence&& other)
    {
      if (this == &other) return *this;

      free(m_data);

      m_data         = other.m_data;
      m_size         = other.m_size;
      m_transmitEnd  = other.m_transmitEnd;
      m_shockerId    = other.m_shockerId;
      m_shockerModel = other.m_shockerModel;

      other.reset();

      return *this;
    }

  private:
    void reset()
    {
      m_data         = nullptr;
      m_size         = 0;
      m_transmitEnd  = 0;
      m_shockerId    = 0;
      m_shockerModel = (ShockerModelType)0;
    }

    rmt_data_t* m_data;
    size_t m_size;
    int64_t m_transmitEnd;
    uint16_t m_shockerId;
    ShockerModelType m_shockerModel;
  };
}  // namespace OpenShock::Rmt
