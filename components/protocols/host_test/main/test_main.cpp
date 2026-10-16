// Host-only (linux target) test binary for the protocols RF bit-encoders.
// FreeRTOS is mocked, so there is no ESP startup - drive Unity directly.
#include "unity.h"

#include "Mockesp_timer.h"

#include "test_clock.h"

int64_t TestClock::NowMs = 0;

static int64_t fakeTimerGetTime(int)
{
  return TestClock::NowMs * 1000;
}

extern "C" int main(void)
{
  esp_timer_get_time_Stub(fakeTimerGetTime);

  UNITY_BEGIN();
  unity_run_all_tests();
  return UNITY_END();
}
