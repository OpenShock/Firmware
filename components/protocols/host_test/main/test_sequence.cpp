// Sequence is the model-dispatch layer over the per-protocol encoders: it sizes
// and allocates the RMT buffer for a shocker model and fills command frames. These
// tests exercise the dispatch for every supported model (a missing case would
// return a zero-size, invalid sequence).
#include "unity.h"

#include "radio/rmt/Sequence.h"

#include <cstdint>

using namespace OpenShock;

TEST_CASE("Sequence builds and fills a CaiXianlin buffer", "[protocols][sequence]")
{
  Rmt::Sequence seq(ShockerModelType::CaiXianlin, 0x4321, 0);

  TEST_ASSERT_TRUE(seq.is_valid());
  TEST_ASSERT_EQUAL(44, seq.size());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ShockerModelType::CaiXianlin), static_cast<int>(seq.shockerModel()));
  TEST_ASSERT_EQUAL_UINT16(0x4321, seq.shockerId());
  TEST_ASSERT_TRUE(seq.fill(ShockerCommandType::Vibrate, 30));
}

TEST_CASE("Sequence dispatches every supported model", "[protocols][sequence]")
{
  const ShockerModelType models[] = {
    ShockerModelType::CaiXianlin,
    ShockerModelType::Petrainer,
    ShockerModelType::Petrainer998DR,
    ShockerModelType::WellturnT330,
    ShockerModelType::D80,
  };

  for (ShockerModelType model : models) {
    Rmt::Sequence seq(model, 1, 0);
    TEST_ASSERT_TRUE(seq.is_valid());
    TEST_ASSERT_GREATER_THAN(0, seq.size());
    TEST_ASSERT_TRUE(seq.fill(ShockerCommandType::Shock, 50));
  }
}

TEST_CASE("Default-constructed Sequence is invalid", "[protocols][sequence]")
{
  Rmt::Sequence seq;
  TEST_ASSERT_FALSE(seq.is_valid());
  TEST_ASSERT_EQUAL(0, seq.size());
}
