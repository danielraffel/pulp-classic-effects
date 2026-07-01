# Panning — static stereo panner (selectable panning law). See README.md.

if(PULP_BUILD_TESTS)
    add_executable(panning-test test_panning.cpp)
    target_link_libraries(panning-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(panning-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(panning-test)
endif()

pulp_add_plugin(Panning
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Panning"
    BUNDLE_ID         "com.pulp.examples.panning"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.0"
    CATEGORY          Effect
    PLUGIN_CODE       "PPan"
    MANUFACTURER_CODE "PlpX"
)
