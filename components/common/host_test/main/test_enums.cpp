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
  ShockerModelType m;
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("caixianlin", m));
  TEST_ASSERT_EQUAL(ShockerModelType::CaiXianlin, m);
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("cai-xianlin", m));
  TEST_ASSERT_EQUAL(ShockerModelType::CaiXianlin, m);
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("petrainer", m));
  TEST_ASSERT_EQUAL(ShockerModelType::Petrainer, m);
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("petrainer998dr", m));
  TEST_ASSERT_EQUAL(ShockerModelType::Petrainer998DR, m);
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("wellturnt330", m));
  TEST_ASSERT_EQUAL(ShockerModelType::WellturnT330, m);
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("t330", m));
  TEST_ASSERT_EQUAL(ShockerModelType::WellturnT330, m);
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("d80", m));
  TEST_ASSERT_EQUAL(ShockerModelType::D80, m);
}

TEST_CASE("ShockerModelType: case-insensitive", "[common][enums]")
{
  ShockerModelType m;
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("CaiXianlin", m));
  TEST_ASSERT_EQUAL(ShockerModelType::CaiXianlin, m);
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("PETRAINER", m));
  TEST_ASSERT_EQUAL(ShockerModelType::Petrainer, m);
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("T330", m));
  TEST_ASSERT_EQUAL(ShockerModelType::WellturnT330, m);
}

TEST_CASE("ShockerModelType: the 'pettrainer' typo only with allowTypo", "[common][enums]")
{
  ShockerModelType m;
  TEST_ASSERT_FALSE(ShockerModelTypeFromString("pettrainer", m));         // default: rejected
  TEST_ASSERT_FALSE(ShockerModelTypeFromString("pettrainer998dr", m));

  TEST_ASSERT_TRUE(ShockerModelTypeFromString("pettrainer", m, true));    // allowTypo: accepted
  TEST_ASSERT_EQUAL(ShockerModelType::Petrainer, m);
  TEST_ASSERT_TRUE(ShockerModelTypeFromString("pettrainer998dr", m, true));
  TEST_ASSERT_EQUAL(ShockerModelType::Petrainer998DR, m);
}

TEST_CASE("ShockerModelType: rejects unknown and empty", "[common][enums]")
{
  ShockerModelType m;
  TEST_ASSERT_FALSE(ShockerModelTypeFromString("", m));
  TEST_ASSERT_FALSE(ShockerModelTypeFromString("nope", m));
  TEST_ASSERT_FALSE(ShockerModelTypeFromString("cai", m));  // prefix, not a full match
}

// ---- ShockerCommandType ----------------------------------------------------

TEST_CASE("ShockerCommandType: all commands + case-insensitive", "[common][enums]")
{
  ShockerCommandType c;
  TEST_ASSERT_TRUE(ShockerCommandTypeFromString("stop", c));
  TEST_ASSERT_EQUAL(ShockerCommandType::Stop, c);
  TEST_ASSERT_TRUE(ShockerCommandTypeFromString("shock", c));
  TEST_ASSERT_EQUAL(ShockerCommandType::Shock, c);
  TEST_ASSERT_TRUE(ShockerCommandTypeFromString("vibrate", c));
  TEST_ASSERT_EQUAL(ShockerCommandType::Vibrate, c);
  TEST_ASSERT_TRUE(ShockerCommandTypeFromString("sound", c));
  TEST_ASSERT_EQUAL(ShockerCommandType::Sound, c);
  TEST_ASSERT_TRUE(ShockerCommandTypeFromString("LIGHT", c));
  TEST_ASSERT_EQUAL(ShockerCommandType::Light, c);
}

TEST_CASE("ShockerCommandType: rejects unknown/empty", "[common][enums]")
{
  ShockerCommandType c;
  TEST_ASSERT_FALSE(ShockerCommandTypeFromString("", c));
  TEST_ASSERT_FALSE(ShockerCommandTypeFromString("zap", c));
}

// ---- OtaUpdateChannel (string_view API) ------------------------------------

TEST_CASE("OtaUpdateChannel: stable/beta/develop/dev + case", "[common][enums]")
{
  OtaUpdateChannel ch;
  TEST_ASSERT_TRUE(TryParseOtaUpdateChannel(ch, "stable"));
  TEST_ASSERT_EQUAL(OtaUpdateChannel::Stable, ch);
  TEST_ASSERT_TRUE(TryParseOtaUpdateChannel(ch, "Beta"));
  TEST_ASSERT_EQUAL(OtaUpdateChannel::Beta, ch);
  TEST_ASSERT_TRUE(TryParseOtaUpdateChannel(ch, "develop"));
  TEST_ASSERT_EQUAL(OtaUpdateChannel::Develop, ch);
  TEST_ASSERT_TRUE(TryParseOtaUpdateChannel(ch, "DEV"));
  TEST_ASSERT_EQUAL(OtaUpdateChannel::Develop, ch);

  TEST_ASSERT_FALSE(TryParseOtaUpdateChannel(ch, ""));
  TEST_ASSERT_FALSE(TryParseOtaUpdateChannel(ch, "nightly"));
  TEST_ASSERT_FALSE(TryParseOtaUpdateChannel(ch, "stabl"));  // prefix, not full
}

// ---- OtaUpdateStep ---------------------------------------------------------

TEST_CASE("OtaUpdateStep: every step + rejects unknown", "[common][enums]")
{
  OtaUpdateStep s;
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep(s, "none"));
  TEST_ASSERT_EQUAL(OtaUpdateStep::None, s);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep(s, "updating"));
  TEST_ASSERT_EQUAL(OtaUpdateStep::Updating, s);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep(s, "updated"));
  TEST_ASSERT_EQUAL(OtaUpdateStep::Updated, s);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep(s, "validating"));
  TEST_ASSERT_EQUAL(OtaUpdateStep::Validating, s);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep(s, "validated"));
  TEST_ASSERT_EQUAL(OtaUpdateStep::Validated, s);
  TEST_ASSERT_TRUE(TryParseOtaUpdateStep(s, "RollingBack"));
  TEST_ASSERT_EQUAL(OtaUpdateStep::RollingBack, s);

  TEST_ASSERT_FALSE(TryParseOtaUpdateStep(s, ""));
  TEST_ASSERT_FALSE(TryParseOtaUpdateStep(s, "done"));
}

// ---- FirmwareBootType (const char* API) ------------------------------------

TEST_CASE("FirmwareBootType: normal/newfirmware/new_firmware/rollback", "[common][enums]")
{
  FirmwareBootType b;
  TEST_ASSERT_TRUE(TryParseFirmwareBootType(b, "normal"));
  TEST_ASSERT_EQUAL(FirmwareBootType::Normal, b);
  TEST_ASSERT_TRUE(TryParseFirmwareBootType(b, "newfirmware"));
  TEST_ASSERT_EQUAL(FirmwareBootType::NewFirmware, b);
  TEST_ASSERT_TRUE(TryParseFirmwareBootType(b, "new_firmware"));
  TEST_ASSERT_EQUAL(FirmwareBootType::NewFirmware, b);
  TEST_ASSERT_TRUE(TryParseFirmwareBootType(b, "ROLLBACK"));
  TEST_ASSERT_EQUAL(FirmwareBootType::Rollback, b);

  TEST_ASSERT_FALSE(TryParseFirmwareBootType(b, ""));
  TEST_ASSERT_FALSE(TryParseFirmwareBootType(b, "reboot"));
}
