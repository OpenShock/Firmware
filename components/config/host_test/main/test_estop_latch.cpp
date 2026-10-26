// The E-Stop latch: state the estop section's module keeps in the section's namespace, outside the section's value.
// A fresh hub starts usable, a stored latch survives a reboot and the move into cfg, and only a factory reset (which
// erases the section) drops it.
#include <freertos/FreeRTOS.h>

#include "unity.h"

#include "config_test_helpers.h"

#include "config/Section.h"

#include <nvs_flash.h>

#include <string>

using namespace OpenShock;
using namespace ConfigTest;
namespace Keys = ConfigTest::Keys;
namespace Nvs  = OpenShock::Config::Nvs;

TEST_CASE("A fresh hub is not latched", "[config][estop]")
{
  InitDefault();
  TEST_ASSERT_FALSE(Config::LoadEStopLatched());
}

TEST_CASE("The E-Stop latch survives a reboot until it is released", "[config][estop]")
{
  InitWith(MakeSampleConfig());
  TEST_ASSERT_TRUE(Config::SaveEStopLatched(true) == Config::LatchSave::Saved);
  Reload();
  TEST_ASSERT_TRUE(Config::LoadEStopLatched());
  AssertConfigEqual(MakeSampleConfig(), Snapshot());  // The latch is not part of the section's value

  TEST_ASSERT_TRUE(Config::SaveEStopLatched(false) == Config::LatchSave::Saved);
  Reload();
  TEST_ASSERT_FALSE(Config::LoadEStopLatched());
}

TEST_CASE("Saving the estop section keeps the latch, and jsonconfig never shows it", "[config][estop]")
{
  InitWith(MakeSampleConfig());
  TEST_ASSERT_TRUE(Config::SaveEStopLatched(true) == Config::LatchSave::Saved);
  TEST_ASSERT_TRUE(Config::SetEStopEnabled(false));
  TEST_ASSERT_TRUE(Config::SetEStopEnabled(true));
  TEST_ASSERT_TRUE(Config::SaveFromJSON(Config::GetAsJSON(true)));
  Reload();
  TEST_ASSERT_TRUE(Config::LoadEStopLatched());
  TEST_ASSERT_TRUE(Config::GetAsJSON(true).find("latch") == std::string::npos);
}

TEST_CASE("Factory reset releases the E-Stop latch", "[config][estop]")
{
  InitWith(MakeSampleConfig());
  TEST_ASSERT_TRUE(Config::SaveEStopLatched(true) == Config::LatchSave::Saved);
  Config::FactoryReset();
  TEST_ASSERT_FALSE(Config::LoadEStopLatched());
  Reload();
  TEST_ASSERT_FALSE(Config::LoadEStopLatched());
}

TEST_CASE("Saving the latch never waits for another config writer", "[config][estop]")
{
  InitWith(MakeSampleConfig());
  {
    // The write lock is a plain mutex, so taking it again from the holder fails like it would from the E-Stop task
    ScopedLock lock__(&Config::Internal::WriteLock());
    TEST_ASSERT_TRUE(Config::SaveEStopLatched(true) == Config::LatchSave::Busy);
  }
  TEST_ASSERT_FALSE(Config::LoadEStopLatched());  // Nothing was written
  TEST_ASSERT_TRUE(Config::SaveEStopLatched(true) == Config::LatchSave::Saved);
  TEST_ASSERT_TRUE(Config::LoadEStopLatched());
}

TEST_CASE("A latch stored in the shared nvs moves into cfg with the estop section", "[config][estop][storage]")
{
  WipeStore();
  TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init_partition(Keys::kPartitionShared));
  TEST_ASSERT_TRUE(MakeSampleConfig().SaveNvs(Keys::kPartitionShared));
  {
    auto estop = Nvs::Handle::Open(Keys::kPartitionShared, Keys::Ns::kEstop, true);
    TEST_ASSERT_TRUE(estop.has_value());
    TEST_ASSERT_TRUE(estop->SetU8(Keys::Estop::kLatched, 1));
    TEST_ASSERT_TRUE(estop->Commit());
  }

  Config::Init();
  TEST_ASSERT_EQUAL_STRING(Keys::kPartitionCfg, Config::StorageLabel());
  TEST_ASSERT_TRUE(Config::LoadEStopLatched());
}

TEST_CASE("A latch stored with another type reads as not latched", "[config][estop]")
{
  InitWith(MakeSampleConfig());
  {
    // NVS looks keys up by name and type, so this is a missing key, not a read error
    auto estop = Nvs::Handle::Open(Keys::kPartitionCfg, Keys::Ns::kEstop, true);
    TEST_ASSERT_TRUE(estop.has_value());
    TEST_ASSERT_TRUE(estop->SetStr(Keys::Estop::kLatched, "yes"));
    TEST_ASSERT_TRUE(estop->Commit());
  }
  Reload();
  TEST_ASSERT_FALSE(Config::LoadEStopLatched());
}
