# Ping-Pong Delay — stereo cross-coupled feedback delay. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(ping-pong-test test_ping_pong.cpp)
    target_link_libraries(ping-pong-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(ping-pong-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(ping-pong-test)
endif()

pulp_add_plugin(PingPong
    FORMATS           VST3 CLAP Standalone
    PLUGIN_NAME       "Ping-Pong Delay"
    BUNDLE_ID         "com.pulp.examples.pingpong"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.1.0"
    CATEGORY          Effect
    PLUGIN_CODE       "PPpd"
    MANUFACTURER_CODE "PlpX"
)
