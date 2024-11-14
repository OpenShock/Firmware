#include "util/DigitCounter.h"

using namespace OpenShock;

static_assert(Util::Digits10CountMax<uint8_t>() == 3, "NumDigits test for uint8_t failed");
static_assert(Util::Digits10CountMax<uint16_t>() == 5, "NumDigits test for uint16_t failed");
static_assert(Util::Digits10CountMax<uint32_t>() == 10, "NumDigits test for uint32_t failed");
static_assert(Util::Digits10CountMax<uint64_t>() == 20, "NumDigits test for uint64_t failed");

static_assert(Util::Digits10CountMax<int8_t>() == 4, "NumDigits test for int8_t failed");
static_assert(Util::Digits10CountMax<int16_t>() == 6, "NumDigits test for int16_t failed");
static_assert(Util::Digits10CountMax<int32_t>() == 11, "NumDigits test for int32_t failed");
static_assert(Util::Digits10CountMax<int64_t>() == 20, "NumDigits test for int64_t failed");
