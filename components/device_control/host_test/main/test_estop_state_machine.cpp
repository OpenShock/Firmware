// EStop debounce + hold-to-clear state machine (driven by EStopManager's 200 Hz task).
// IsEStopped() is LastEStopped() != 0, which mirrors activatedAt(), so every state but
// Idle must refuse commands.
#include "unity.h"

#include "estop/EStopStateMachine.h"

#include <cstdint>

using namespace OpenShock;

namespace {
  const int kPressed  = 0;  // Input is pulled up; the button pulls it low
  const int kReleased = 1;

  const int64_t kSampleMs = 5;  // EStopManager samples at 200 Hz

  struct Rig {
    EStopStateMachine machine;
    int64_t now = 1'000'000;

    void sample(int level)
    {
      now += kSampleMs;
      machine.Sample(level, now);
    }

    void feed(int level, int count)
    {
      for (int i = 0; i < count; i++) {
        sample(level);
      }
    }

    // Holds the input at `level` until the clock has advanced by at least `ms`.
    void hold(int level, int64_t ms)
    {
      int64_t until = now + ms;
      while (now < until) {
        sample(level);
      }
    }

    // Drives Idle -> Active -> (released) -> ActiveClearing -> AwaitingRelease -> Idle.
    void activateAndClear()
    {
      sample(kPressed);
      feed(kReleased, EStopStateMachine::kCheckCount);
      sample(kPressed);
      hold(kPressed, EStopStateMachine::kHoldToClearTime);
      feed(kReleased, EStopStateMachine::kCheckCount);
    }

    EStopState state() const { return machine.state(); }
    bool estopped() const { return machine.activatedAt() != 0; }
  };
}  // namespace

static void assertState(EStopState expected, const Rig& rig)
{
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(rig.state()));
  TEST_ASSERT_EQUAL(expected != EStopState::Idle, rig.estopped());
}

TEST_CASE("EStop starts Idle and stays Idle while released", "[device_control][estop]")
{
  Rig rig;
  assertState(EStopState::Idle, rig);

  rig.hold(kReleased, 10'000);
  assertState(EStopState::Idle, rig);
}

TEST_CASE("EStop activates on the first pressed sample and records the time", "[device_control][estop]")
{
  Rig rig;
  rig.sample(kPressed);

  assertState(EStopState::Active, rig);
  TEST_ASSERT_EQUAL_INT64(rig.now, rig.machine.activatedAt());
}

TEST_CASE("EStop holding the activating press never clears it", "[device_control][estop]")
{
  // The press that tripped the e-stop is not a press edge in Active, so keeping the
  // button down must not start hold-to-clear.
  Rig rig;
  rig.sample(kPressed);
  int64_t activatedAt = rig.machine.activatedAt();

  rig.hold(kPressed, 3 * EStopStateMachine::kHoldToClearTime);
  assertState(EStopState::Active, rig);

  rig.feed(kReleased, EStopStateMachine::kCheckCount);
  assertState(EStopState::Active, rig);
  TEST_ASSERT_EQUAL_INT64(activatedAt, rig.machine.activatedAt());
}

TEST_CASE("EStop release is debounced over kCheckCount samples", "[device_control][estop]")
{
  Rig rig;
  rig.sample(kPressed);

  // One short of a full window of released samples still reads as pressed, so a
  // new press does not register as an edge.
  rig.feed(kReleased, EStopStateMachine::kCheckCount - 1);
  rig.sample(kPressed);
  assertState(EStopState::Active, rig);

  rig.feed(kReleased, EStopStateMachine::kCheckCount);
  rig.sample(kPressed);
  assertState(EStopState::ActiveClearing, rig);
}

TEST_CASE("EStop a bounce while released reads as pressed", "[device_control][estop]")
{
  Rig rig;
  rig.sample(kPressed);
  rig.feed(kReleased, EStopStateMachine::kCheckCount);

  // A single low sample in the window is enough to count as a press edge.
  rig.feed(kReleased, 3);
  rig.sample(kPressed);
  rig.sample(kReleased);
  assertState(EStopState::ActiveClearing, rig);
}

TEST_CASE("EStop clearing requires holding for kHoldToClearTime", "[device_control][estop]")
{
  Rig rig;
  rig.sample(kPressed);
  rig.feed(kReleased, EStopStateMachine::kCheckCount);

  rig.sample(kPressed);
  int64_t edgeAt = rig.now;
  assertState(EStopState::ActiveClearing, rig);

  while (rig.now + kSampleMs < edgeAt + EStopStateMachine::kHoldToClearTime) {
    rig.sample(kPressed);
    assertState(EStopState::ActiveClearing, rig);
  }

  rig.sample(kPressed);
  TEST_ASSERT_EQUAL_INT64(edgeAt + EStopStateMachine::kHoldToClearTime, rig.now);
  assertState(EStopState::AwaitingRelease, rig);

  // Still e-stopped until the button is let go, however long it is held.
  rig.hold(kPressed, 10'000);
  assertState(EStopState::AwaitingRelease, rig);
}

TEST_CASE("EStop releasing before the hold time returns to Active", "[device_control][estop]")
{
  Rig rig;
  rig.sample(kPressed);
  rig.feed(kReleased, EStopStateMachine::kCheckCount);

  rig.sample(kPressed);
  rig.hold(kPressed, EStopStateMachine::kHoldToClearTime - 1000);
  assertState(EStopState::ActiveClearing, rig);

  rig.feed(kReleased, EStopStateMachine::kCheckCount);
  assertState(EStopState::Active, rig);

  // The aborted attempt does not count towards the next one.
  rig.sample(kPressed);
  rig.hold(kPressed, EStopStateMachine::kHoldToClearTime - 1000);
  assertState(EStopState::ActiveClearing, rig);
}

TEST_CASE("EStop clears to Idle on debounced release after the hold", "[device_control][estop]")
{
  Rig rig;
  rig.sample(kPressed);
  rig.feed(kReleased, EStopStateMachine::kCheckCount);
  rig.sample(kPressed);
  rig.hold(kPressed, EStopStateMachine::kHoldToClearTime);
  assertState(EStopState::AwaitingRelease, rig);

  rig.feed(kReleased, EStopStateMachine::kCheckCount - 1);
  assertState(EStopState::AwaitingRelease, rig);

  rig.sample(kReleased);
  assertState(EStopState::Idle, rig);
  TEST_ASSERT_EQUAL_INT64(0, rig.machine.activatedAt());
}

TEST_CASE("EStop ignores presses during the rearm grace window", "[device_control][estop]")
{
  Rig rig;
  rig.activateAndClear();
  assertState(EStopState::Idle, rig);
  int64_t clearedAt = rig.now;

  // Release bounce right after clearing: debounced press lasts kCheckCount samples
  // but ends before the grace window does.
  rig.sample(kPressed);
  rig.feed(kReleased, EStopStateMachine::kCheckCount);
  TEST_ASSERT_TRUE(rig.now < clearedAt + EStopStateMachine::kRearmGraceTime);
  assertState(EStopState::Idle, rig);

  rig.hold(kReleased, EStopStateMachine::kRearmGraceTime);
  assertState(EStopState::Idle, rig);

  // Re-armed: the next press trips it again.
  rig.sample(kPressed);
  assertState(EStopState::Active, rig);
}

TEST_CASE("EStop a press held through the grace window trips once it ends", "[device_control][estop]")
{
  Rig rig;
  rig.activateAndClear();
  int64_t clearedAt = rig.now;

  rig.sample(kPressed);
  while (rig.now + kSampleMs < clearedAt + EStopStateMachine::kRearmGraceTime) {
    rig.sample(kPressed);
    assertState(EStopState::Idle, rig);
  }

  rig.sample(kPressed);
  assertState(EStopState::Active, rig);
}

TEST_CASE("EStop software trigger activates from Idle", "[device_control][estop]")
{
  Rig rig;
  rig.now += 123;
  rig.machine.Trigger(rig.now);

  assertState(EStopState::Active, rig);
  TEST_ASSERT_EQUAL_INT64(rig.now, rig.machine.activatedAt());

  // Releasing does nothing; it still needs the hold-to-clear sequence.
  rig.hold(kReleased, 10'000);
  assertState(EStopState::Active, rig);
}

TEST_CASE("EStop software trigger keeps the original activation time", "[device_control][estop]")
{
  Rig rig;
  rig.sample(kPressed);
  int64_t activatedAt = rig.machine.activatedAt();

  rig.hold(kPressed, 1000);
  rig.machine.Trigger(rig.now);
  TEST_ASSERT_EQUAL_INT64(activatedAt, rig.machine.activatedAt());
}

TEST_CASE("EStop software trigger cancels an in-progress clear", "[device_control][estop]")
{
  Rig rig;
  rig.sample(kPressed);
  rig.feed(kReleased, EStopStateMachine::kCheckCount);
  rig.sample(kPressed);
  rig.hold(kPressed, 1000);
  assertState(EStopState::ActiveClearing, rig);

  rig.machine.Trigger(rig.now);
  assertState(EStopState::Active, rig);

  // The button is still held, so there is no new edge to restart clearing.
  rig.hold(kPressed, 2 * EStopStateMachine::kHoldToClearTime);
  assertState(EStopState::Active, rig);
}

TEST_CASE("EStop software trigger cancels AwaitingRelease", "[device_control][estop]")
{
  Rig rig;
  rig.sample(kPressed);
  rig.feed(kReleased, EStopStateMachine::kCheckCount);
  rig.sample(kPressed);
  rig.hold(kPressed, EStopStateMachine::kHoldToClearTime);
  assertState(EStopState::AwaitingRelease, rig);

  rig.machine.Trigger(rig.now);
  rig.feed(kReleased, EStopStateMachine::kCheckCount);
  assertState(EStopState::Active, rig);
}

TEST_CASE("EStop software trigger overrides the rearm grace window", "[device_control][estop]")
{
  Rig rig;
  rig.activateAndClear();
  assertState(EStopState::Idle, rig);

  rig.sample(kReleased);
  rig.machine.Trigger(rig.now);
  assertState(EStopState::Active, rig);
}

TEST_CASE("EStop is e-stopped in every state except Idle", "[device_control][estop]")
{
  // Pseudo-random button noise plus occasional software triggers; the invariant
  // behind IsEStopped() must hold after every step.
  Rig rig;
  uint32_t lcg       = 12345;
  bool sawState[4]   = {};
  int64_t lastActive = 0;

  for (int i = 0; i < 200'000; i++) {
    lcg = lcg * 1664525u + 1013904223u;

    uint32_t r = lcg >> 16;
    if (r % 5000 == 0) {
      rig.now += kSampleMs;
      rig.machine.Trigger(rig.now);
    } else {
      // Long runs of each level so hold-to-clear can actually complete.
      int level = ((r >> 4) % 4000) < 1500 ? kPressed : kReleased;
      if (((lcg >> 8) & 0x3FF) == 0) {
        rig.hold(level, 6000);
      } else {
        rig.sample(level);
      }
    }

    EStopState state                      = rig.state();
    sawState[static_cast<uint8_t>(state)] = true;
    TEST_ASSERT_EQUAL(state != EStopState::Idle, rig.estopped());

    if (state != EStopState::Idle) {
      // The activation time never moves while the e-stop stays active.
      if (lastActive != 0) {
        TEST_ASSERT_EQUAL_INT64(lastActive, rig.machine.activatedAt());
      }
      lastActive = rig.machine.activatedAt();
    } else {
      lastActive = 0;
    }
  }

  TEST_ASSERT_TRUE(sawState[static_cast<uint8_t>(EStopState::Idle)]);
  TEST_ASSERT_TRUE(sawState[static_cast<uint8_t>(EStopState::Active)]);
  TEST_ASSERT_TRUE(sawState[static_cast<uint8_t>(EStopState::ActiveClearing)]);
}
