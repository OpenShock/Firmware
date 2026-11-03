# The C++ standard every OpenShock build compiles with. Included right after project() by the firmware's CMakeLists.txt
# and by every components/*/host_test/CMakeLists.txt, so the firmware and its host tests speak the same language.
#
# ESP-IDF appends -std=gnu++26 for chip targets and the newest -std the host compiler accepts for the linux target.
# GCC's C++26 mode is experimental and, from GCC 14 to 17, miscompiles a constexpr function that searches a string
# (string_view::find and anything on top of it) when the call is folded with literal arguments: the folded value
# disagrees with strict constant evaluation and with a runtime call (GCC bug 127126, -std=c++2c only). The device
# toolchain is GCC 15.2, so the firmware is as exposed as the common host test that first showed it. This flag comes
# after ESP-IDF's and the last -std on the command line wins. Drop it once the toolchain carries the fix.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++23" APPEND)
