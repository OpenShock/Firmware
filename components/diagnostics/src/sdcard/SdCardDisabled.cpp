#include "diagnostics/SdCard.h"

// SD card support compiled out (CONFIG_OPENSHOCK_DIAG_SDCARD off): the API exists and reports Disabled.
#if !CONFIG_OPENSHOCK_DIAG_SDCARD

using namespace OpenShock::Diagnostics;

const char* SdCard::StateName(State state)
{
  return state == State::Disabled ? "disabled" : "unknown";
}

SdCard::Info SdCard::GetInfo()
{
  return Info {.state = State::Disabled, .cardSectors = 0, .epoch = 0, .nextSeq = 0, .boot = 0, .droppedRecords = 0, .droppedBytes = 0, .nextDumpSeq = 0};
}

bool SdCard::Format()
{
  return false;
}

bool SdCard::Eject()
{
  return false;
}

void SdCard::RecordTimeSync(int64_t, TimeSource)
{
}

#endif
