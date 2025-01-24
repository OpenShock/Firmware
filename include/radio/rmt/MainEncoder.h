#pragma once

#include "Common.h"
#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <esp32-hal-rmt.h>

#include <cstdint>

namespace OpenShock::Rmt {
  class MainEncoder {
    DISABLE_COPY(MainEncoder);

  public:
    MainEncoder()
      : m_data(nullptr)
      , m_size(0)
      , m_shockerModel()
      , m_shockerId(0)
    {
    }
    MainEncoder(ShockerModelType shockerModel, uint16_t shockerId);
    MainEncoder(MainEncoder&& other) noexcept
      : m_data(other.m_data)
      , m_size(other.m_size)
      , m_shockerModel(other.m_shockerModel)
      , m_shockerId(other.m_shockerId)
    {
      other.m_data      = nullptr;
      other.m_size      = 0;
      other.m_shockerId = 0;
    }
    ~MainEncoder() { free(m_data); }

    inline bool is_valid() const noexcept { return m_data != nullptr && m_size > 0; }

    inline ShockerModelType shockerModel() const noexcept { return m_shockerModel; }
    inline uint16_t shockerId() const noexcept { return m_shockerId; }

    inline rmt_data_t* payload() noexcept { return m_data; }
    inline const rmt_data_t* payload() const noexcept { return m_data; }
    inline rmt_data_t* terminator() noexcept { return m_data + m_size; }
    inline const rmt_data_t* terminator() const noexcept { return m_data + m_size; }
    inline size_t size() const noexcept { return m_size; }

    bool fillSequence(ShockerCommandType commandType, uint8_t intensity);

    MainEncoder& operator=(MainEncoder&& other)
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
    ShockerModelType m_shockerModel;
    uint16_t m_shockerId;
  };
}  // namespace OpenShock::Rmt
