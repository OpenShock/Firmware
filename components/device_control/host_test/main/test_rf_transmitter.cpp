// RFTransmitter::TransmitTask, the consumer half of the e-stop interlock: commands
// that reach its queue while e-stopped must be dropped, and live commands must be cut
// to their terminator frames the moment the e-stop trips.
#include "unity.h"

#include "frames.h"
#include "host_fakes.h"
#include "radio/RFTransmitter.h"

#include <cstdint>
#include <vector>

using namespace OpenShock;

namespace {
  const gpio_num_t kPin                = static_cast<gpio_num_t>(5);
  const ShockerModelType kModel        = ShockerModelType::CaiXianlin;
  const uint16_t kShockerId            = 4321;
  const int kBudget                    = 10'000;
  const ShockerModelType kUnknownModel = static_cast<ShockerModelType>(200);
}  // namespace

static TaskHandle_t transmitTask()
{
  TaskHandle_t task = HostFake::FindTask("RFTransmitter-5");
  TEST_ASSERT_NOT_NULL(task);
  return task;
}

TEST_CASE("RFTransmitter sends the payload for the duration, then terminators", "[device_control][rf]")
{
  HostFake::Reset();
  RFTransmitter tx(kPin);
  TEST_ASSERT_TRUE(tx.ok());

  auto shock      = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Shock, 50);
  auto terminator = TestFrames::Terminator(kModel, kShockerId);

  int64_t start = HostFake::Now;
  TEST_ASSERT_TRUE(tx.SendCommand(kModel, kShockerId, ShockerCommandType::Shock, 50, 200));

  // Returns false: the task finished every sequence and went back to waiting.
  TEST_ASSERT_FALSE(HostFake::RunTask(transmitTask(), kBudget));

  TEST_ASSERT_TRUE(TestFrames::Count(shock) > 0);
  TEST_ASSERT_TRUE(TestFrames::Count(terminator) > 0);
  TEST_ASSERT_EQUAL(HostFake::Transmissions.size(), TestFrames::Count(shock) + TestFrames::Count(terminator));

  for (const auto& frame : HostFake::Transmissions) {
    if (frame.symbols == shock) {
      TEST_ASSERT_TRUE(frame.at < start + 200);
    } else {
      // Terminators follow the payload for the 300 ms terminator window.
      TEST_ASSERT_TRUE(frame.at >= start + 200);
      TEST_ASSERT_TRUE(frame.at < start + 200 + 300);
    }
  }
}

TEST_CASE("RFTransmitter drops commands queued while e-stopped", "[device_control][rf][estop]")
{
  HostFake::Reset();
  RFTransmitter tx(kPin);

  HostFake::EStopped = true;
  TEST_ASSERT_TRUE(tx.SendCommand(kModel, kShockerId, ShockerCommandType::Shock, 100, 1000));
  TEST_ASSERT_TRUE(tx.SendCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 100, 1000));
  TEST_ASSERT_TRUE(tx.SendCommand(kModel, kShockerId + 1, ShockerCommandType::Sound, 100, 1000));

  TEST_ASSERT_FALSE(HostFake::RunTask(transmitTask(), kBudget));

  TEST_ASSERT_EQUAL(0, HostFake::Transmissions.size());
}

TEST_CASE("RFTransmitter cuts live commands to terminators when the e-stop trips", "[device_control][rf][estop]")
{
  HostFake::Reset();
  RFTransmitter tx(kPin);

  auto shock      = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Shock, 80);
  auto otherShock = TestFrames::Payload(kModel, kShockerId + 1, ShockerCommandType::Shock, 80);
  auto lateCmd    = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Vibrate, 30);
  auto terminator = TestFrames::Terminator(kModel, kShockerId);

  TEST_ASSERT_TRUE(tx.SendCommand(kModel, kShockerId, ShockerCommandType::Shock, 80, 10'000));
  TEST_ASSERT_TRUE(tx.SendCommand(kModel, kShockerId + 1, ShockerCommandType::Shock, 80, 10'000));

  // Trip the e-stop mid-transmission, and have a command race in right behind it.
  size_t trippedAfter  = 0;
  HostFake::OnTransmit = [&] {
    if (!HostFake::EStopped && HostFake::Transmissions.size() == 6) {
      HostFake::EStopped = true;
      trippedAfter       = HostFake::Transmissions.size();
      tx.SendCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 30, 10'000);
    }
  };

  int64_t start = HostFake::Now;
  TEST_ASSERT_FALSE(HostFake::RunTask(transmitTask(), kBudget));
  TEST_ASSERT_EQUAL(6, trippedAfter);

  for (size_t i = 0; i < HostFake::Transmissions.size(); i++) {
    const auto& symbols = HostFake::Transmissions[i].symbols;
    if (i < trippedAfter) {
      TEST_ASSERT_TRUE(symbols == shock || symbols == otherShock);
    } else {
      TEST_ASSERT_FALSE(symbols == shock);
      TEST_ASSERT_FALSE(symbols == otherShock);
      TEST_ASSERT_FALSE(symbols == lateCmd);
    }
  }

  TEST_ASSERT_TRUE(TestFrames::Count(terminator) > 0);
  TEST_ASSERT_TRUE(TestFrames::Count(TestFrames::Terminator(kModel, kShockerId + 1)) > 0);

  // Both 10 s commands were finished off within the terminator window, not their duration.
  TEST_ASSERT_TRUE(HostFake::Transmissions.back().at < start + 1000);
}

TEST_CASE("RFTransmitter resumes once the e-stop clears", "[device_control][rf][estop]")
{
  HostFake::Reset();
  RFTransmitter tx(kPin);

  auto dropped  = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Shock, 90);
  auto accepted = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Vibrate, 20);

  HostFake::EStopped = true;
  TEST_ASSERT_TRUE(tx.SendCommand(kModel, kShockerId, ShockerCommandType::Shock, 90, 1000));

  // Once the task is idle, clear the e-stop and send a fresh command.
  bool cleared     = false;
  HostFake::OnIdle = [&] {
    if (!cleared) {
      cleared            = true;
      HostFake::EStopped = false;
      tx.SendCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 20, 200);
    }
  };

  TEST_ASSERT_FALSE(HostFake::RunTask(transmitTask(), kBudget));
  TEST_ASSERT_TRUE(cleared);

  TEST_ASSERT_EQUAL(0, TestFrames::Count(dropped));
  TEST_ASSERT_TRUE(TestFrames::Count(accepted) > 0);
}

TEST_CASE("RFTransmitter Stop replaces a live command with a short zero-intensity vibrate", "[device_control][rf]")
{
  HostFake::Reset();
  RFTransmitter tx(kPin);

  auto shock = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Shock, 80);
  auto stop  = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Vibrate, 0);

  TEST_ASSERT_TRUE(tx.SendCommand(kModel, kShockerId, ShockerCommandType::Shock, 80, 10'000));

  size_t stoppedAfter  = 0;
  HostFake::OnTransmit = [&] {
    if (stoppedAfter == 0 && HostFake::Transmissions.size() == 3) {
      stoppedAfter = HostFake::Transmissions.size();
      // Intensity/duration are ignored for Stop.
      tx.SendCommand(kModel, kShockerId, ShockerCommandType::Stop, 99, 10'000, false);
    }
  };

  int64_t start = HostFake::Now;
  TEST_ASSERT_FALSE(HostFake::RunTask(transmitTask(), kBudget));

  for (size_t i = stoppedAfter; i < HostFake::Transmissions.size(); i++) {
    TEST_ASSERT_TRUE(HostFake::Transmissions[i].symbols == stop);
  }
  TEST_ASSERT_EQUAL(stoppedAfter, TestFrames::Count(shock));
  TEST_ASSERT_TRUE(HostFake::Transmissions.back().at < start + 1000);
}

TEST_CASE("RFTransmitter drops commands for an unknown shocker model", "[device_control][rf]")
{
  HostFake::Reset();
  RFTransmitter tx(kPin);

  auto valid = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Vibrate, 40);

  tx.SendCommand(kUnknownModel, kShockerId, ShockerCommandType::Shock, 100, 1000);
  TEST_ASSERT_TRUE(tx.SendCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 40, 100));

  TEST_ASSERT_FALSE(HostFake::RunTask(transmitTask(), kBudget));

  // Only the valid command made it to the air.
  TEST_ASSERT_TRUE(TestFrames::Count(valid) > 0);
  TEST_ASSERT_EQUAL(HostFake::Transmissions.size(), TestFrames::Count(valid) + TestFrames::Count(TestFrames::Terminator(kModel, kShockerId)));
}

TEST_CASE("RFTransmitter exits on destroy", "[device_control][rf]")
{
  HostFake::Reset();
  size_t queuesBefore = HostFake::LiveQueueCount();
  TaskHandle_t task;
  {
    RFTransmitter tx(kPin);
    task = transmitTask();
    TEST_ASSERT_EQUAL(queuesBefore + 1, HostFake::LiveQueueCount());
  }

  TEST_ASSERT_TRUE(HostFake::TaskStopped(task));
  TEST_ASSERT_EQUAL(queuesBefore, HostFake::LiveQueueCount());
}

TEST_CASE("RFTransmitter is not ok when the RMT channel cannot be created", "[device_control][rf]")
{
  HostFake::Reset();
  HostFake::FailRmtChannel = true;

  RFTransmitter tx(kPin);
  TEST_ASSERT_FALSE(tx.ok());
  TEST_ASSERT_FALSE(tx.SendCommand(kModel, kShockerId, ShockerCommandType::Shock, 10, 100));

  HostFake::Reset();
}
