# Compile config's sources directly (not via the config component, whose logging
# dependency pulls in app_update, which has no linux port). Config.cpp is included,
# so the tests drive the real Config:: store (Init, SetRaw, GetAsJSON / SaveFromJSON)
# under the real ReadWriteMutex, and Config::Init finds its partition in the
# firmware's real partition table through ESP-IDF's linux esp_partition. Only the
# filesystem is faked: stubs/fs/ConfigFs.h is an in-memory file store the tests seed
# and inspect. osjson's Json.cpp and the common sources config needs are compiled in
# as well (jsmn / json_generator come from idf_component.yml).
idf_component_register(
  SRCS         "test_main.cpp"
               "config_test_helpers.cpp"
               "test_roundtrip.cpp"   # flatbuffer + jsonconfig round trips through the store
               "test_defaults.cpp"    # missing file / sections / fields
               "test_legacy.cpp"      # configs written by the Arduino firmware (legacy_fixtures.h)
               "test_corrupt.cpp"     # truncated / garbage / tampered input
               "../../src/BackendConfig.cpp"
               "../../src/CaptivePortalConfig.cpp"
               "../../src/Config.cpp"
               "../../src/EStopConfig.cpp"
               "../../src/OtaUpdateConfig.cpp"
               "../../src/RFConfig.cpp"
               "../../src/RootConfig.cpp"
               "../../src/SerialInputConfig.cpp"
               "../../src/WiFiConfig.cpp"
               "../../src/WiFiCredentials.cpp"
               "../../src/internal/utils.cpp"
               "../../../osjson/src/Json.cpp"
               "../../../common/src/Convert.cpp"        # Json.cpp strict string->int
               "../../../common/src/DigitCounter.cpp"
               "../../../common/src/ReadWriteMutex.cpp" # Config.cpp's store lock
               "../../../common/src/StringHelpers.cpp"
  INCLUDE_DIRS "."
               "stubs"                                   # fs/ConfigFs.h (in-memory)
               "../../include"                           # config/*.h
               "../../../chipset/include"                # Chipset.h
               "../../../common/include"                 # OpenShock.h, ReadWriteMutex.h, TinyVec.h, enums/*, util/HexUtils.h
               "../../../osjson/include"                 # json/Json.h
               "../../../serialization/include"          # serialization/_fbs/HubConfig_generated.h
               "../../../flatbuffers/flatbuffers/include"
  REQUIRES     unity
               host_support                              # Logging.h, openshock_board.h, ESP32 GPIO caps (test/host_support)
               freertos                                  # ReadWriteMutex (real POSIX port)
               esp_partition                             # Config::Init's partition lookup (linux flash emulation)
               driver                                    # Chipset.h -> driver/gpio.h (tools/mocks/driver)
               esp_wifi                                  # WiFiCredentials.h -> esp_wifi_types.h (tools/mocks/esp_wifi)
               esp_hal_gpio                              # hal/gpio_types.h (gpio_num_t)
               espressif__jsmn
               espressif__json_generator
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)

# Same as the config component itself.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
