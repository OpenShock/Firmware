// Host-only (linux target) test binary for device_control's safety logic: the EStop
// state machine and the CommandHandler/RFTransmitter e-stop interlock. FreeRTOS is
// mocked (CMock, see host_fakes.cpp), so there is no ESP startup - drive Unity directly.
#include "unity.h"

#include "BoardDescriptor.h"

extern "C" int main(void)
{
  OpenShock::Board::Init();  // The test board (test/host_support), as app_main does on the device

  UNITY_BEGIN();
  unity_run_all_tests();
  return UNITY_END();
}
