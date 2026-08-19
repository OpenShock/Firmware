// FnProxy<&Class::method> yields a plain R(*)(void*, Ts...) trampoline used to
// hand C++ member functions to C-style callback APIs (FreeRTOS tasks, esp_event).
// Verify it forwards args and returns for the const/non-const overloads.
#include "unity.h"

#include "util/FnProxy.h"

#include <type_traits>

using OpenShock::Util::FnProxy;

namespace {
  struct Counter {
    int value = 0;
    int add(int x) { return value += x; }
    int get() const noexcept { return value; }
  };
}  // namespace

// noexcept propagates onto the function-pointer type; plain methods stay plain.
static_assert(std::is_same_v<std::remove_const_t<decltype(FnProxy<&Counter::add>)>,
                             int (*)(void*, int)>);
static_assert(std::is_same_v<std::remove_const_t<decltype(FnProxy<&Counter::get>)>,
                             int (*)(void*) noexcept>);

TEST_CASE("FnProxy forwards to a non-const member", "[util][fnproxy]")
{
  Counter c;
  auto fn = FnProxy<&Counter::add>;  // int(*)(void*, int)

  TEST_ASSERT_EQUAL_INT(5, fn(&c, 5));
  TEST_ASSERT_EQUAL_INT(8, fn(&c, 3));
  TEST_ASSERT_EQUAL_INT(8, c.value);
}

TEST_CASE("FnProxy forwards to a const noexcept member", "[util][fnproxy]")
{
  Counter c;
  c.value = 42;
  auto fn = FnProxy<&Counter::get>;  // int(*)(void*)

  TEST_ASSERT_EQUAL_INT(42, fn(&c));
}
