// Host-only (linux target) test binary for the diagnostics component's pure logic.
#include "unity.h"

extern "C" int main(void)
{
  UNITY_BEGIN();
  unity_run_all_tests();
  return UNITY_END();
}
