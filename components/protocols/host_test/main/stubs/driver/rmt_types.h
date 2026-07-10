#pragma once

// Host-test stub for the IDF <driver/rmt_types.h>. The real header pulls in
// hal/soc types (gpio_types.h etc.) that don't build on the linux target. The RF
// encoders only need rmt_symbol_word_t; replicate it exactly so the encoded
// timing/level bit layout under test matches the firmware.
#include <cstdint>

typedef union {
  struct {
    uint16_t duration0 : 15;  // Duration of level0
    uint16_t level0    : 1;   // Level of the first part
    uint16_t duration1 : 15;  // Duration of level1
    uint16_t level1    : 1;   // Level of the second part
  };
  uint32_t val;
} rmt_symbol_word_t;
