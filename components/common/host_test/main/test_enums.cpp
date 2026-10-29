// Parser coverage for the shared enum headers. These decide how user/backend
// strings map to shocker models, commands, OTA channels/steps and boot types -
// getting them wrong misroutes real commands, so pin every accepted spelling.
#include "unity.h"

#include "enums/FirmwareBootType.h"
#include "enums/OtaUpdateChannel.h"
#include "enums/OtaUpdateStep.h"
#include "enums/ShockerCommandType.h"
#include "enums/ShockerModelType.h"

using namespace OpenShock;

// ---- ShockerModelType ------------------------------------------------------

TEST_CASE("ShockerModelType: all accepted spellings", "[common][enums]")
{
  TEST_ASSERT_TRUE(TryParseShockerModelType("caixianlin") == ShockerModelType::CaiXianlin);
  TEST_ASSERT_TRUE(TryParseShockerModelType("cai-xianlin") == ShockerModelType::CaiXianlin);
  TEST_ASSERT_TRUE(TryParseShockerModelType("petrainer") == ShockerModelType::Petrainer);
  TEST_ASSERT_TRUE(TryParseShockerModelType("petrainer998dr") == ShockerModelType::Petrainer998DR);
  TEST_ASSERT_TRUE(TryParseShockerModelType("wellturnt330") == ShockerModelType::WellturnT330);
  TEST_ASSERT_TRUE(TryParseShockerModelType("t330") == ShockerModelType::WellturnT330);
  TEST_ASSERT_TRUE(TryParseShockerModelType("d80") == ShockerModelType::D80);
  TEST_ASSERT_TRUE(TryParseShockerModelType("wellturnt330alt1") == ShockerModelType::WellturnT330Alt1);
  TEST_ASSERT_TRUE(TryParseShockerModelType("t330alt1") == ShockerModelType::WellturnT330Alt1);
}

TEST_CASE("ShockerModelType: case-insensitive", "[common][enums]")
{
  TEST_ASSERT_TRUE(TryParseShockerModelType("CaiXianlin") == ShockerModelType::CaiXianlin);
  TEST_ASSERT_TRUE(TryParseShockerModelType("PETRAINER") == ShockerModelType::Petrainer);
  TEST_ASSERT_TRUE(TryParseShockerModelType("T330") == ShockerModelType::WellturnT330);
}

TEST_CASE("ShockerModelType: the 'pettrainer' typo only with allowTypo", "[common][enums]")
{
  TEST_ASSERT_FALSE(TryParseShockerModelType("pettrainer").has_value());  // default: rejected
  TEST_ASSERT_FALSE(TryParseShockerModelType("pettrainer998dr").has_value());

  TEST_ASSERT_TRUE(TryParseShockerModelType("pettrainer", true) == ShockerModelType::Petrainer);  // allowTypo: accepted
  TEST_ASSERT_TRUE(TryParseShockerModelType("pettrainer998dr", true) == ShockerModelType::Petrainer998DR);
}

TEST_CASE("ShockerModelType: rejects unknown and empty", "[common][enums]")
{
  TEST_ASSERT_FALSE(TryParseShockerModelType("").has_value());
  TEST_ASSERT_FALSE(TryParseShockerModelType("nope").has_value());
  TEST_ASSERT_FALSE(TryParseShockerModelType("cai").has_value());  // prefix, not a full match
}

// ---- ShockerCommandType ----------------------------------------------------

TEST_CASE("ShockerCommandType: all commands + case-insensitive", "[common][enums]")
{
  TEST_ASSERT_TRUE(TryParseShockerCommandType("stop") == ShockerCommandType::Stop);
  TEST_ASSERT_TRUE(TryParseShockerCommandType("shock") == ShockerCommandType::Shock);
  TEST_ASSERT_TRUE(TryParseShockerCommandType("vibrate") == ShockerCommandType::Vibrate);
  TEST_ASSERT_TRUE(TryParseShockerCommandType("sound") == ShockerCommandType::Sound);
  TEST_ASSERT_TRUE(TryParseShockerCommandType("LIGHT") == ShockerCommandType::Light);
}

TEST_CASE("ShockerCommandType: rejects unknown/empty", "[common][enums]")
{
  TEST_ASSERT_FALSE(TryParseShockerCommandType("").has_value());
  TEST_ASSERT_FALSE(TryParseShockerCommandType("zap").has_value());
}

// ---- OtaUpdateChannel (string_view API) ------------------------------------

TEST_CASE("OtaUpdateChannel: stable/beta/develop/dev + case", "[common][enums]")
{
  TEST_ASSERT_TRUE(TryParseOtaUpdateChannel("stable") == OtaUpdateChannel::Stable);
  TEST_ASSERT_TRUE(TryParseOtaUpdateChannel("Beta") == OtaUpdateChannel::Beta);
  TEST_ASSERT_TRUE(TryParseOtaUpdateChannel("develop") == OtaUpdateChannel::Develop);
  TEST_ASSERT_TRUE(TryParseOtaUpdateChannel("DEV") == OtaUpdateChannel::Develop);

  TEST_ASSERT_FALSE(TryParseOtaUpdateChannel("").has_value());
  TEST_ASSERT_FALSE(TryParseOtaUpdateChannel("nightly").has_value());
  TEST_ASSERT_FALSE(TryParseOtaUpdateChannel("stabl").has_value());  // prefix, not full
}

// ---- OtaUpdateStep ---------------------------------------------------------

TEST_CASE("OtaUpdateStep: every step + rejects unknown", "[common][enums]")
{
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep("none") == OtaUpdateStep::None);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep("updating") == OtaUpdateStep::Updating);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep("updated") == OtaUpdateStep::Updated);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep("validating") == OtaUpdateStep::Validating);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep("validated") == OtaUpdateStep::Validated);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep("RollingBack") == OtaUpdateStep::RollingBack);

  TEST_ASSERT_FALSE(TryParseOtaUpdateStep("").has_value());
  TEST_ASSERT_FALSE(TryParseOtaUpdateStep("done").has_value());
}

// ---- FirmwareBootType (string_view API) ------------------------------------

TEST_CASE("FirmwareBootType: normal/newfirmware/new_firmware/rollback", "[common][enums]")
{
  TEST_ASSERT_TRUE(TryParseFirmwareBootType("normal") == FirmwareBootType::Normal);
  TEST_ASSERT_TRUE(TryParseFirmwareBootType("newfirmware") == FirmwareBootType::NewFirmware);
  TEST_ASSERT_TRUE(TryParseFirmwareBootType("new_firmware") == FirmwareBootType::NewFirmware);
  TEST_ASSERT_TRUE(TryParseFirmwareBootType("ROLLBACK") == FirmwareBootType::Rollback);

  TEST_ASSERT_FALSE(TryParseFirmwareBootType("").has_value());
  TEST_ASSERT_FALSE(TryParseFirmwareBootType("reboot").has_value());
}

TEST_CASE("InferFirmwareBootType: maps every OTA step to a boot type", "[common][enums]")
{
  // Only 'Updated' means we just flashed new firmware this boot.
  TEST_ASSERT_EQUAL(FirmwareBootType::NewFirmware, InferFirmwareBootType(OtaUpdateStep::Updated));

  // A crash while validating, or an explicit rolling-back step, means this boot is
  // the previous image after a rollback.
  TEST_ASSERT_EQUAL(FirmwareBootType::Rollback, InferFirmwareBootType(OtaUpdateStep::Validating));
  TEST_ASSERT_EQUAL(FirmwareBootType::Rollback, InferFirmwareBootType(OtaUpdateStep::RollingBack));

  // Everything else is a normal boot.
  TEST_ASSERT_EQUAL(FirmwareBootType::Normal, InferFirmwareBootType(OtaUpdateStep::None));
  TEST_ASSERT_EQUAL(FirmwareBootType::Normal, InferFirmwareBootType(OtaUpdateStep::Updating));
  TEST_ASSERT_EQUAL(FirmwareBootType::Normal, InferFirmwareBootType(OtaUpdateStep::Validated));
}
