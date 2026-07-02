# Ring Modulator — multiply the signal by a sine carrier. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(ring-mod-test test_ring_mod.cpp)
    target_link_libraries(ring-mod-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(ring-mod-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(ring-mod-test)
endif()

pulp_add_plugin(RingMod
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Ring Mod"
    BUNDLE_ID         "com.pulp.examples.ringmod"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.3"
    CATEGORY          Effect
    PLUGIN_CODE       "PRng"
    MANUFACTURER_CODE "PlpX"
)
