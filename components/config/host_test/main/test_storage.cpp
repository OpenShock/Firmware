// Where the config lives: the `cfg` partition, the shared `nvs` only until the layout migration creates `cfg`,
// and the one-time move between them on the first boot of the new layout.
#include <freertos/FreeRTOS.h>

#include "unity.h"

#include "config_test_helpers.h"

#include <nvs_flash.h>

#include <iterator>

using namespace OpenShock;
using namespace ConfigTest;
namespace Keys = ConfigTest::Keys;
namespace Nvs  = OpenShock::Config::Nvs;

namespace {
  bool HasSchema(const char* partition)
  {
    auto root = Nvs::Handle::Open(partition, Keys::Ns::kRoot, false);
    if (!root) return false;
    uint8_t schema = 0;
    return root->GetU8(Keys::Root::kSchema, schema) == Nvs::Result::Found && schema == Keys::kSchemaVersion;
  }

  // A hub on the 1.x layout kept its config in the shared nvs; this stands that in on a table that has cfg too.
  void StoreInShared(const Full& config)
  {
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init_partition(Keys::kPartitionShared));
    TEST_ASSERT_TRUE(config.SaveNvs(Keys::kPartitionShared));
    TEST_ASSERT_TRUE(HasSchema(Keys::kPartitionShared));
  }
}  // namespace

TEST_CASE("The store is the cfg partition", "[config][storage]")
{
  InitDefault();
  TEST_ASSERT_EQUAL_STRING(Keys::kPartitionCfg, Config::StorageLabel());
  TEST_ASSERT_TRUE(HasSchema(Keys::kPartitionCfg));
  TEST_ASSERT_FALSE(HasSchema(Keys::kPartitionShared));  // nothing of ours goes in the shared partition
}

TEST_CASE("A config kept in the shared nvs moves into cfg on the first boot that has cfg", "[config][storage]")
{
  WipeStore();
  const Full sample = MakeSampleConfig();
  StoreInShared(sample);

  Config::Init();
  TEST_ASSERT_EQUAL_STRING(Keys::kPartitionCfg, Config::StorageLabel());
  AssertConfigEqual(sample, Snapshot());
  TEST_ASSERT_TRUE(HasSchema(Keys::kPartitionCfg));

  // The shared copy is gone: every key of every namespace of ours (NVS keeps an emptied namespace's name)
  TEST_ASSERT_FALSE(HasSchema(Keys::kPartitionShared));
  for (const char* ns : Keys::Ns::kAll) {
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, Nvs::KeyCount(Keys::kPartitionShared, ns), ns);
  }

  // And a later boot is a plain load from cfg
  Reload();
  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("A config in cfg wins over a stale copy in the shared nvs, which is cleaned up", "[config][storage]")
{
  InitWith(MakeSampleConfig());
  Full stale          = MakeSampleConfig();
  stale.wifi.hostname = "stale-hub";
  StoreInShared(stale);

  Reload();
  AssertConfigEqual(MakeSampleConfig(), Snapshot());
  TEST_ASSERT_FALSE(HasSchema(Keys::kPartitionShared));
}

TEST_CASE("The move is idempotent and verified", "[config][storage]")
{
  WipeStore();
  StoreInShared(MakeSampleConfig());
  Config::Init();
  Reload();
  Reload();
  AssertConfigEqual(MakeSampleConfig(), Snapshot());
  TEST_ASSERT_TRUE(HasSchema(Keys::kPartitionCfg));
}

TEST_CASE("Sections link themselves into the registry in document order", "[config][storage]")
{
  const char* expected[] = {"rf", "wifi", "captivePortal", "backend", "serialInput", "otaUpdate", "estop"};
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
    auto rf = Nvs::Handle::Open(Keys::kPartitionCfg, Keys::Ns::kRf, true);
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
