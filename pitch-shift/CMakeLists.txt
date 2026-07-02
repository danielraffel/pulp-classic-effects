# Pitch Shift — phase-vocoder pitch shifter. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(pitch-shift-test test_pitch_shift.cpp)
    target_link_libraries(pitch-shift-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(pitch-shift-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(pitch-shift-test)
endif()

pulp_add_plugin(PitchShift
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Pitch Shift"
    BUNDLE_ID         "com.pulp.examples.pitch-shift"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.5"
    CATEGORY          Effect
    PLUGIN_CODE       "PPit"
    MANUFACTURER_CODE "PlpX"
)
