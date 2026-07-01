# Delay — feedback delay line with wet/dry mix. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(delay-test test_delay.cpp)
    target_link_libraries(delay-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(delay-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(delay-test)
endif()

pulp_add_plugin(Delay
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Delay"
    BUNDLE_ID         "com.pulp.examples.delay"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.2"
    CATEGORY          Effect
    PLUGIN_CODE       "PDly"
    MANUFACTURER_CODE "PlpX"
)
