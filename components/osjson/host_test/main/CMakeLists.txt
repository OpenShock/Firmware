# Compile osjson's Json.cpp (and the common sources it uses for strict string->int
# conversion) directly, rather than linking the osjson component: osjson -> common ->
# logging -> app_update, which has no linux port. jsmn / json_generator come from
# idf_component.yml, the same versions osjson declares.
idf_component_register(
  SRCS         "test_json.cpp"       # main() entry + smoke tests
               "test_parse.cpp"      # JsonDocument::parse + root()
               "test_getters.cpp"    # tryGetStr/Bool/I64/Double + predicates
               "test_navigation.cpp" # operator[], at(), count(), raw(), skip()
               "test_generate.cpp"   # StringWriter / json_generator output
               "test_stress.cpp"     # scale, token-cap, deep nesting
               "../../src/Json.cpp"
               "../../../common/src/Convert.cpp"
               "../../../common/src/DigitCounter.cpp"
               "../../../common/src/StringHelpers.cpp"
  INCLUDE_DIRS "."
               "../../include"            # json/Json.h
               "../../../common/include"  # Convert.h, StringHelpers.h, util/DigitCounter.h
  REQUIRES     unity
               host_support               # Logging.h, openshock_board.h (test/host_support)
               esp_hal_gpio               # Convert.h -> hal/gpio_types.h
               espressif__jsmn
               espressif__json_generator
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)
