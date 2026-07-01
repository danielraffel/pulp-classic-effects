# Robotization — STFT zero-phase monotone voice. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(robotization-test test_robotization.cpp)
    target_link_libraries(robotization-test PRIVATE pulp::format pulp::signal pulp::view Catch2::Catch2WithMain)
    target_include_directories(robotization-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(robotization-test)
endif()

pulp_add_plugin(Robotization
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Robotization"
    BUNDLE_ID         "com.pulp.examples.robotization"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.2"
    CATEGORY          Effect
    PLUGIN_CODE       "PRbt"
    MANUFACTURER_CODE "PlpX"
)
