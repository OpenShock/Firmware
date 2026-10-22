// Host-only (linux target) test binary for the protocols RF bit-encoders.
// FreeRTOS is mocked, so there is no ESP startup - drive Unity directly.
#include "unity.h"

extern "C" int main(void)
{
  UNITY_BEGIN();
  unity_run_all_tests();
  return UNITY_END();
}
