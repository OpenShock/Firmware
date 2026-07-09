idf_component_register(
  SRCS         "test_main.cpp"
               "test_enums.cpp"
               "test_stringhelpers.cpp"
  INCLUDE_DIRS "."
               "../../include"  # common's headers (enums/*, StringHelpers.h) - header-only
  REQUIRES     unity
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)
