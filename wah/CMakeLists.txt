# Wah — resonant bandpass swept manually or by the input envelope. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(wah-test test_wah.cpp)
    target_link_libraries(wah-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(wah-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(wah-test)
endif()

pulp_add_plugin(Wah
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Wah-Wah"
    BUNDLE_ID         "com.pulp.examples.wah"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.4"
    CATEGORY          Effect
    PLUGIN_CODE       "PWah"
    MANUFACTURER_CODE "PlpX"
)
