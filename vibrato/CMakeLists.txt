# Vibrato — LFO-swept fractional delay (pitch modulation). See README.md.

if(PULP_BUILD_TESTS)
    add_executable(vibrato-test test_vibrato.cpp)
    target_link_libraries(vibrato-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(vibrato-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(vibrato-test)
endif()

pulp_add_plugin(Vibrato
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Vibrato"
    BUNDLE_ID         "com.pulp.examples.vibrato"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.2"
    CATEGORY          Effect
    PLUGIN_CODE       "PVib"
    MANUFACTURER_CODE "PlpX"
)
