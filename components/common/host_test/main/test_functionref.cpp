// FunctionRef<Sig> is a non-owning, trivially copyable callable reference (a
// stand-in for C++26 std::function_ref). Verify it calls lambdas with captures,
// plain function pointers and const functors, and passes return values through.
#include "unity.h"

#include "util/FunctionRef.h"

#include <string>
#include <type_traits>

using OpenShock::Util::FunctionRef;

namespace {
  int Twice(int x)
  {
    return x * 2;
  }

  struct Adder {
    int base;
    int operator()(int x) const { return base + x; }
  };

  int CallWith(FunctionRef<int(int)> fn, int x)
  {
    return fn(x);
  }

  void CallVoid(FunctionRef<void(int)> fn, int x)
  {
    fn(x);
  }
}  // namespace

static_assert(std::is_trivially_copyable_v<FunctionRef<int(int)>>);
static_assert(sizeof(FunctionRef<int(int)>) == 2 * sizeof(void*));
static_assert(!std::is_default_constructible_v<FunctionRef<int(int)>>);
static_assert(!std::is_constructible_v<FunctionRef<int(int)>, int>);
static_assert(!std::is_constructible_v<FunctionRef<int(int)>, void (*)(std::string)>);

TEST_CASE("FunctionRef calls a capturing lambda", "[util][functionref]")
{
  int calls = 0;
  int sum   = 0;
  auto fn   = [&](int x) {
    ++calls;
    sum += x;
    return sum;
  };

  TEST_ASSERT_EQUAL_INT(3, CallWith(fn, 3));
  TEST_ASSERT_EQUAL_INT(7, CallWith(fn, 4));
  TEST_ASSERT_EQUAL_INT(2, calls);  // referenced, not copied: state lives in the lambda's captures
  TEST_ASSERT_EQUAL_INT(7, sum);
}

TEST_CASE("FunctionRef calls a function pointer", "[util][functionref]")
{
  TEST_ASSERT_EQUAL_INT(10, CallWith(Twice, 5));
  TEST_ASSERT_EQUAL_INT(10, CallWith(&Twice, 5));

  // The pointer itself is stored, so the FunctionRef outlives the pointer variable.
  FunctionRef<int(int)> ref = [] {
    int (*ptr)(int) = Twice;
    return FunctionRef<int(int)>(ptr);
  }();
  TEST_ASSERT_EQUAL_INT(14, ref(7));
}

TEST_CASE("FunctionRef calls a const functor", "[util][functionref]")
{
  const Adder add {100};
  TEST_ASSERT_EQUAL_INT(142, CallWith(add, 42));
}

TEST_CASE("FunctionRef discards the result for a void signature", "[util][functionref]")
{
  int seen = 0;
  CallVoid([&](int x) { return seen = x; }, 9);  // non-void callable into void(int)
  TEST_ASSERT_EQUAL_INT(9, seen);
}

TEST_CASE("FunctionRef copies refer to the same callable", "[util][functionref]")
{
  int counter = 0;
  auto inc    = [&](int x) { return counter += x; };

  FunctionRef<int(int)> a = inc;
  FunctionRef<int(int)> b = a;  // copy, not a re-wrap
  a(1);
  b(2);
  TEST_ASSERT_EQUAL_INT(3, counter);
}
