// CommandHandler, the producer half of the e-stop interlock, and the keep-alive task
// it manages. CommandHandler is a process-wide singleton, so every test starts from
// resetCommandHandler(): one Init(), then a fresh transmitter and keep-alive off.
#include "unity.h"

#include "CommandHandler.h"
#include "estop/EStopState.h"
#include "frames.h"
#include "host_fakes.h"

#include <cstdint>

using namespace OpenShock;

namespace {
  const gpio_num_t kTxPin       = static_cast<gpio_num_t>(4);
  const ShockerModelType kModel = ShockerModelType::Petrainer;
  const uint16_t kShockerId     = 777;
  const int kBudget             = 10'000;
  const int64_t kKeepAliveMs    = 60'000;
}  // namespace

static void resetCommandHandler()
{
  HostFake::Reset();

  static bool initialized = false;
  if (!initialized) {
    HostFake::RfConfig = {.txPin = kTxPin, .keepAliveEnabled = false};
    TEST_ASSERT_TRUE(CommandHandler::Init());
    TEST_ASSERT_NOT_NULL(HostFake::EStopHandler);
    initialized = true;
  }

  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(false));
  TEST_ASSERT_EQUAL(SetGPIOResultCode::Success, CommandHandler::SetRfTxPin(kTxPin));
  TEST_ASSERT_TRUE(CommandHandler::Ok());
}

static TaskHandle_t transmitTask()
{
  TaskHandle_t task = HostFake::FindTask("RFTransmitter-4");
  TEST_ASSERT_NOT_NULL(task);
  return task;
}

static void deliverEStopState(EStopState state)
{
  HostFake::EStopHandler(nullptr, OPENSHOCK_EVENTS, OPENSHOCK_EVENT_ESTOP_STATE_CHANGED, &state);
}

TEST_CASE("CommandHandler transmits commands while not e-stopped", "[device_control][command]")
{
  resetCommandHandler();

  auto shock = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Shock, 25);

  TEST_ASSERT_TRUE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Shock, 25, 300));
  HostFake::RunTask(transmitTask(), kBudget);

  TEST_ASSERT_TRUE(TestFrames::Count(shock) > 0);
}

TEST_CASE("CommandHandler refuses commands while e-stopped", "[device_control][command][estop]")
{
  resetCommandHandler();

  HostFake::EStopped = true;
  TEST_ASSERT_FALSE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Shock, 100, 1000));
  TEST_ASSERT_FALSE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 100, 1000));
  TEST_ASSERT_FALSE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Sound, 0, 1000));

  // Nothing even reached the transmitter's queue.
  HostFake::EStopped = false;
  HostFake::RunTask(transmitTask(), kBudget);
  TEST_ASSERT_EQUAL(0, HostFake::Transmissions.size());
}

TEST_CASE("CommandHandler command accepted just before an e-stop is still dropped by the transmitter", "[device_control][command][estop]")
{
  // Producer passes the check, then the e-stop trips before the RF task dequeues
  // it: the consumer-side check must catch it.
  resetCommandHandler();

  TEST_ASSERT_TRUE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Shock, 100, 1000));
  HostFake::EStopped = true;

  HostFake::RunTask(transmitTask(), kBudget);
  TEST_ASSERT_EQUAL(0, HostFake::Transmissions.size());
}

TEST_CASE("CommandHandler zero-duration commands only send terminators", "[device_control][command]")
{
  resetCommandHandler();

  auto shock = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Shock, 100);

  TEST_ASSERT_TRUE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Shock, 100, 0));
  HostFake::RunTask(transmitTask(), kBudget);

  TEST_ASSERT_EQUAL(0, TestFrames::Count(shock));
  TEST_ASSERT_TRUE(TestFrames::Count(TestFrames::Terminator(kModel, kShockerId)) > 0);
  TEST_ASSERT_EQUAL(HostFake::Transmissions.size(), TestFrames::Count(TestFrames::Terminator(kModel, kShockerId)));
}

TEST_CASE("CommandHandler never transmits an unknown shocker model", "[device_control][command]")
{
  resetCommandHandler();

  // HandleCommand does not validate the model (the encoder lookup in the RF task
  // rejects it), so only assert nothing goes on air.
  CommandHandler::HandleCommand(static_cast<ShockerModelType>(200), kShockerId, ShockerCommandType::Shock, 100, 1000);
  HostFake::RunTask(transmitTask(), kBudget);

  TEST_ASSERT_EQUAL(0, HostFake::Transmissions.size());
}

TEST_CASE("CommandHandler refuses commands without a transmitter", "[device_control][command]")
{
  resetCommandHandler();

  // A pin the (ESP32) chipset accepts, so the RMT channel failure is what is hit.
  HostFake::FailRmtChannel = true;
  TEST_ASSERT_EQUAL(SetGPIOResultCode::InternalError, CommandHandler::SetRfTxPin(static_cast<gpio_num_t>(13)));
  HostFake::FailRmtChannel = false;

  TEST_ASSERT_FALSE(CommandHandler::Ok());
  TEST_ASSERT_FALSE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Shock, 10, 100));
}

TEST_CASE("CommandHandler invalid TX pin keeps the current transmitter", "[device_control][command]")
{
  resetCommandHandler();

  TEST_ASSERT_EQUAL(SetGPIOResultCode::InvalidPin, CommandHandler::SetRfTxPin(static_cast<gpio_num_t>(99)));
  TEST_ASSERT_EQUAL(SetGPIOResultCode::InvalidPin, CommandHandler::SetRfTxPin(GPIO_NUM_NC));

  TEST_ASSERT_TRUE(CommandHandler::Ok());
  TEST_ASSERT_EQUAL(kTxPin, CommandHandler::GetRfTxPin());
  TEST_ASSERT_TRUE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 10, 100));
}

TEST_CASE("CommandHandler keep-alive enable/disable starts and stops its task", "[device_control][command][keepalive]")
{
  resetCommandHandler();
  size_t queuesBefore = HostFake::LiveQueueCount();
  TEST_ASSERT_NULL(HostFake::FindTask("KeepAliveTask"));

  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(true));
  TEST_ASSERT_TRUE(HostFake::RfConfig.keepAliveEnabled);
  TaskHandle_t keepAlive = HostFake::FindTask("KeepAliveTask");
  TEST_ASSERT_NOT_NULL(keepAlive);
  TEST_ASSERT_EQUAL(queuesBefore + 1, HostFake::LiveQueueCount());

  // Enabling twice does not start a second task.
  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(true));
  TEST_ASSERT_TRUE(HostFake::FindTask("KeepAliveTask") == keepAlive);

  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(false));
  TEST_ASSERT_FALSE(HostFake::RfConfig.keepAliveEnabled);
  TEST_ASSERT_TRUE(HostFake::TaskStopped(keepAlive));
  TEST_ASSERT_NULL(HostFake::FindTask("KeepAliveTask"));
  TEST_ASSERT_EQUAL(queuesBefore, HostFake::LiveQueueCount());

  // With keep-alive off, commands still work and nothing is queued for it.
  TEST_ASSERT_TRUE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 10, 100));
  TEST_ASSERT_EQUAL(queuesBefore, HostFake::LiveQueueCount());
}

TEST_CASE("CommandHandler only feeds keep-alive for accepted commands", "[device_control][command][keepalive][estop]")
{
  resetCommandHandler();
  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(true));
  QueueHandle_t keepAliveQueue = HostFake::NewestQueue();

  TEST_ASSERT_TRUE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 10, 100));
  TEST_ASSERT_EQUAL(1, HostFake::QueueLength(keepAliveQueue));

  HostFake::EStopped = true;
  TEST_ASSERT_FALSE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 10, 100));
  TEST_ASSERT_EQUAL(1, HostFake::QueueLength(keepAliveQueue));

  HostFake::EStopped = false;
  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(false));
}

TEST_CASE("CommandHandler keep-alive pings a shocker after a minute of inactivity", "[device_control][command][keepalive]")
{
  resetCommandHandler();
  QueueHandle_t rfQueue = HostFake::NewestQueue();
  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(true));
  QueueHandle_t keepAliveQueue = HostFake::NewestQueue();
  TaskHandle_t keepAlive       = HostFake::FindTask("KeepAliveTask");

  int64_t commandEnd = HostFake::Now + 500;
  TEST_ASSERT_TRUE(CommandHandler::HandleCommand(kModel, kShockerId, ShockerCommandType::Vibrate, 10, 500));
  TEST_ASSERT_EQUAL(1, HostFake::QueueLength(keepAliveQueue));

  // Play the command out so the transmitter's queue is empty again.
  HostFake::RunTask(transmitTask(), kBudget);
  TEST_ASSERT_EQUAL(0, HostFake::QueueLength(rfQueue));
  HostFake::Transmissions.clear();

  // Step the keep-alive task until it hands something to the transmitter.
  int64_t pingedAt = 0;
  HostFake::OnIdle = [&] {
    if (HostFake::QueueLength(rfQueue) > 0) {
      pingedAt = HostFake::Now;
      throw HostFake::TaskBlocked {};
    }
  };
  HostFake::RunTask(keepAlive, kBudget);
  HostFake::OnIdle = nullptr;

  TEST_ASSERT_NOT_EQUAL(0, pingedAt);
  TEST_ASSERT_TRUE(pingedAt > commandEnd + kKeepAliveMs);
  TEST_ASSERT_TRUE(pingedAt <= commandEnd + kKeepAliveMs + 10);
  TEST_ASSERT_EQUAL(1, HostFake::QueueLength(rfQueue));

  // The ping is a silent 0-intensity vibrate.
  auto ping = TestFrames::Payload(kModel, kShockerId, ShockerCommandType::Vibrate, 0);
  HostFake::RunTask(transmitTask(), kBudget);
  TEST_ASSERT_FALSE(HostFake::Transmissions.empty());
  TEST_ASSERT_EQUAL(HostFake::Transmissions.size(), TestFrames::Count(ping));

  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(false));
}

TEST_CASE("CommandHandler e-stop events suspend keep-alive until Idle", "[device_control][command][keepalive][estop]")
{
  resetCommandHandler();
  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(true));
  TaskHandle_t keepAlive = HostFake::FindTask("KeepAliveTask");
  TEST_ASSERT_NOT_NULL(keepAlive);

  deliverEStopState(EStopState::Active);
  TEST_ASSERT_TRUE(HostFake::TaskStopped(keepAlive));
  TEST_ASSERT_NULL(HostFake::FindTask("KeepAliveTask"));

  deliverEStopState(EStopState::ActiveClearing);
  deliverEStopState(EStopState::AwaitingRelease);
  TEST_ASSERT_NULL(HostFake::FindTask("KeepAliveTask"));

  deliverEStopState(EStopState::Idle);
  TEST_ASSERT_NOT_NULL(HostFake::FindTask("KeepAliveTask"));

  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(false));
}

TEST_CASE("CommandHandler clearing an e-stop leaves keep-alive off when disabled in config", "[device_control][command][keepalive][estop]")
{
  resetCommandHandler();
  TEST_ASSERT_FALSE(HostFake::RfConfig.keepAliveEnabled);
  TEST_ASSERT_NULL(HostFake::FindTask("KeepAliveTask"));

  deliverEStopState(EStopState::Active);
  deliverEStopState(EStopState::Idle);

  TEST_ASSERT_NULL(HostFake::FindTask("KeepAliveTask"));
  TEST_ASSERT_FALSE(HostFake::RfConfig.keepAliveEnabled);
}

TEST_CASE("CommandHandler keep-alive enabled while e-stopped starts only once the e-stop clears", "[device_control][command][keepalive][estop]")
{
  resetCommandHandler();

  HostFake::EStopped = true;
  deliverEStopState(EStopState::Active);

  // The setting is saved, but the task stays off while e-stopped.
  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(true));
  TEST_ASSERT_TRUE(HostFake::RfConfig.keepAliveEnabled);
  TEST_ASSERT_NULL(HostFake::FindTask("KeepAliveTask"));

  HostFake::EStopped = false;
  deliverEStopState(EStopState::Idle);
  TEST_ASSERT_NOT_NULL(HostFake::FindTask("KeepAliveTask"));

  TEST_ASSERT_TRUE(CommandHandler::SetKeepAliveEnabled(false));
}
