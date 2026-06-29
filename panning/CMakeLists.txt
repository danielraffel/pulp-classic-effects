# Auto-Pan — equal-power LFO stereo panner. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(panning-test test_panning.cpp)
    target_link_libraries(panning-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(panning-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(panning-test)
endif()

pulp_add_plugin(Panning
    FORMATS           VST3 CLAP Standalone
    PLUGIN_NAME       "Auto-Pan"
    BUNDLE_ID         "com.pulp.examples.panning"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.1.0"
    CATEGORY          Effect
    PLUGIN_CODE       "PPan"
    MANUFACTURER_CODE "PlpX"
)
