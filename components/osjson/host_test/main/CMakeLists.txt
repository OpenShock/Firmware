idf_component_register(
  SRCS         "test_json.cpp"       # main() entry + smoke tests
               "test_parse.cpp"      # JsonDocument::parse + root()
               "test_getters.cpp"    # tryGetStr/Bool/I64/Double + predicates
               "test_navigation.cpp" # operator[], at(), count(), raw(), skip()
               "test_generate.cpp"   # StringWriter / json_generator output
               "test_stress.cpp"     # scale, token-cap, deep nesting
  INCLUDE_DIRS "."
  REQUIRES     unity osjson
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)
