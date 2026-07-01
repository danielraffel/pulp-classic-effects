# Flanger — short LFO-swept delay with feedback. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(flanger-test test_flanger.cpp)
    target_link_libraries(flanger-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(flanger-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(flanger-test)
endif()

pulp_add_plugin(Flanger
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Flanger"
    BUNDLE_ID         "com.pulp.examples.flanger"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.0"
    CATEGORY          Effect
    PLUGIN_CODE       "PFlg"
    MANUFACTURER_CODE "PlpX"
)
