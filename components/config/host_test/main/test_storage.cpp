// Where the config lives: the `cfg` partition.
#include <freertos/FreeRTOS.h>

#include "unity.h"

#include "config_test_helpers.h"

#include <iterator>

using namespace OpenShock;
using namespace ConfigTest;
namespace Keys = ConfigTest::Keys;
namespace Nvs  = OpenShock::Config::Nvs;

TEST_CASE("The store is the cfg partition", "[config][storage]")
{
  InitDefault();
  TEST_ASSERT_EQUAL_STRING(Keys::kPartitionCfg, Config::StorageLabel());
}

TEST_CASE("Sections link themselves into the registry in document order", "[config][storage]")
{
  const char* expected[] = {"rf", "wifi", "captivePortal", "backend", "serialInput", "otaUpdate", "estop", "led"};
  std::size_t i          = 0;
  for (auto* section = Config::SectionBase::First(); section != nullptr; section = section->Next(), ++i) {
    TEST_ASSERT_LESS_THAN_size_t(std::size(expected), i);
    TEST_ASSERT_EQUAL_STRING(expected[i], section->Info().name);
  }
  TEST_ASSERT_EQUAL_size_t(std::size(expected), i);
}

TEST_CASE("A key stored with another type reads as missing, so as its default", "[config][storage]")
{
  InitWith(MakeSampleConfig());
  {
    // NVS looks keys up by name and type: a string where the rf section reads an i8 is not its pin
    auto rf = Nvs::Handle::OpenForWrite(Keys::kPartitionCfg, Keys::Ns::kRf);
    TEST_ASSERT_TRUE(rf.has_value());
    TEST_ASSERT_TRUE(rf->Erase(Keys::Rf::kTxPin));
    TEST_ASSERT_TRUE(rf->SetStr(Keys::Rf::kTxPin, "not a number"));
    TEST_ASSERT_TRUE(rf->Commit());
  }
  Reload();

  Full expected     = MakeSampleConfig();
  expected.rf.txPin = Config::RFConfig().txPin;
  AssertConfigEqual(expected, Snapshot());
}
