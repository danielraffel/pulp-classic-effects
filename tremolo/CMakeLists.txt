# Tremolo — periodic amplitude modulation. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(tremolo-test test_tremolo.cpp)
    target_link_libraries(tremolo-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(tremolo-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(tremolo-test)
endif()

pulp_add_plugin(Tremolo
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Tremolo"
    BUNDLE_ID         "com.pulp.examples.tremolo"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.3"
    CATEGORY          Effect
    PLUGIN_CODE       "PTrm"
    MANUFACTURER_CODE "PlpX"
)
