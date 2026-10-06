# Compile device_control's CommandHandler + RFTransmitter (and what they use from
# protocols, common and events) directly, against the real OpenShock and ESP-IDF
# headers. The IDF APIs are CMock mocks whose callbacks (host_fakes.cpp) step a fake
# clock; config (stubs/config/Config.h), TaskUtils and EStopManager are faked in
# host_fakes.cpp too, so a test can run the transmit/keep-alive task loops
# deterministically. EStopManager's debounce/hold-to-clear logic is tested through
# the pure EStopStateMachine.h.
idf_component_register(
  SRCS         "test_main.cpp"
               "host_fakes.cpp"
               "test_estop_state_machine.cpp"
               "test_rf_transmitter.cpp"
               "test_command_handler.cpp"
               "../../src/CommandHandler.cpp"
               "../../src/radio/RFTransmitter.cpp"
               "../../../common/src/SimpleMutex.cpp"
               "../../../events/src/Events.cpp"
               "../../../protocols/src/Sequence.cpp"
               "../../../protocols/src/CaiXianlinEncoder.cpp"
               "../../../protocols/src/D80Encoder.cpp"
               "../../../protocols/src/Petrainer998DREncoder.cpp"
               "../../../protocols/src/PetrainerEncoder.cpp"
               "../../../protocols/src/T330Encoder.cpp"
  INCLUDE_DIRS "."
               "stubs"                      # config/Config.h (fake RF/E-Stop settings)
               "../../src"                  # estop/EStopStateMachine.h
               "../../include"              # CommandHandler.h, estop/*, radio/RFTransmitter.h
               "../../../chipset/include"   # Chipset.h
               "../../../common/include"    # OpenShock.h, SimpleMutex.h, enums/*, Checksum.h, util/*
               "../../../events/include"    # events/Events.h
               "../../../protocols/include" # radio/rmt/Sequence.h + encoders
               "../../../protocols/src"     # radio/rmt/internal/Shared.h
               "../../../temporal/include"  # Temporal.h (esp_timer_get_time)
  REQUIRES     unity
               host_support                 # Logging.h, openshock_board.h, ESP32 SoC caps (test/host_support)
               freertos                     # tools/mocks/freertos
               driver                       # tools/mocks/driver: driver/rmt_*.h, driver/gpio.h
               esp_event                    # tools/mocks/esp_event
               esp_timer                    # tools/mocks/esp_timer
               esp_hal_gpio                 # hal/gpio_types.h
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)

# Same as the firmware component: format-string width warnings (%hhi on gpio_num_t)
# are not errors.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
