# The config framework (components/config) and every module's config section, compiled directly (not via their
# components, whose logging dependency pulls in app_update, which has no linux port). So the tests drive the store
# as the firmware assembles it: Config::Init, the modules' accessors, jsonconfig, rawconfig and factory reset, under
# the real SimpleMutexes, against ESP-IDF's real NVS over the linux flash emulation and the partition table in
# ../partitions_test.csv. Only the 1.x littlefs reader is faked: stubs/fs/LfsPartition.h is an in-memory file store
# the tests seed with the config file a 1.x hub leaves behind. osjson's Json.cpp and the common sources needed are
# compiled in as well (jsmn / json_generator come from idf_component.yml).
set(_components ../../..)
idf_component_register(
  SRCS         "test_main.cpp"
               "config_test_helpers.cpp"
               "test_roundtrip.cpp"   # API + jsonconfig + rawconfig round trips through the NVS store
               "test_defaults.cpp"    # empty store / missing namespaces / missing keys
               "test_legacy.cpp"      # the 1.x config file (legacy_fixtures.h) imported on first boot
               "test_corrupt.cpp"     # truncated / garbage / tampered input
               "test_storage.cpp"     # store selection, the move from the shared nvs into cfg, the registry
               # The framework
               "${_components}/config/src/Nvs.cpp"
               "${_components}/config/src/Parse.cpp"
               "${_components}/config/src/Section.cpp"
               "${_components}/config/src/Store.cpp"
               "${_components}/config/src/Wire.cpp"
               # Each module's section
               "${_components}/device_control/src/radio/RFConfig.cpp"
               "${_components}/device_control/src/estop/EStopConfig.cpp"
               "${_components}/core/src/wifi/WiFiConfig.cpp"
               "${_components}/core/src/wifi/WiFiCredentials.cpp"
               "${_components}/core/src/BackendConfig.cpp"
               "${_components}/core/src/OtaUpdateConfig.cpp"
               "${_components}/captive_portal/src/captiveportal/CaptivePortalConfig.cpp"
               "${_components}/serial_console/src/SerialInputConfig.cpp"
               # What they use
               "${_components}/osjson/src/Json.cpp"
               "${_components}/common/src/Convert.cpp"        # Json.cpp strict string->int; credential keys
               "${_components}/common/src/SimpleMutex.cpp"    # the section and store locks
               "${_components}/common/src/StringHelpers.cpp"
  INCLUDE_DIRS "."
               "stubs"                                         # fs/LfsPartition.h (in-memory)
               "${_components}/config/include"                 # config/*.h
               "${_components}/device_control/include"         # radio/RFConfig.h, estop/EStopConfig.h
               "${_components}/core/include"                   # wifi/WiFiConfig.h, BackendConfig.h, OtaUpdateConfig.h
               "${_components}/captive_portal/include"         # captiveportal/CaptivePortalConfig.h
               "${_components}/serial_console/include"         # serial_console/SerialInputConfig.h
               "${_components}/chipset/include"                # Chipset.h
               "${_components}/common/include"                 # OpenShock.h, SimpleMutex.h, TinyVec.h, enums/*, util/HexUtils.h
               "${_components}/osjson/include"                 # json/Json.h
               "${_components}/serialization/include"          # serialization/_fbs/HubConfig_generated.h, VerifiedRoot.h
               "${_components}/flatbuffers/flatbuffers/include"
  REQUIRES     unity
               host_support                              # Logging.h, openshock_board.h, ESP32 GPIO caps (test/host_support)
               freertos                                  # SimpleMutex (real POSIX port)
               esp_partition                             # the partition table (linux flash emulation)
               nvs_flash                                 # the store itself
               esp_wifi                                  # WiFiCredentials.h -> esp_wifi_types.h (tools/mocks/esp_wifi)
               esp_hal_gpio                              # hal/gpio_types.h (gpio_num_t)
               espressif__jsmn
               espressif__json_generator
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs (and the sections' registry entries) from being stripped
)

# Same as the config component itself.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
