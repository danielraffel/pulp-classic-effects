# Distortion — tanh waveshaper with tone, level, and mix. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(distortion-test test_distortion.cpp)
    target_link_libraries(distortion-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(distortion-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(distortion-test)
endif()

pulp_add_plugin(Distortion
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Distortion"
    BUNDLE_ID         "com.pulp.examples.distortion"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.5"
    CATEGORY          Effect
    PLUGIN_CODE       "PDst"
    MANUFACTURER_CODE "PlpX"
)
