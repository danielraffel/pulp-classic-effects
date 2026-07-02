# Chorus — LFO-swept short delay blended with the dry signal. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(chorus-test test_chorus.cpp)
    target_link_libraries(chorus-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(chorus-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(chorus-test)
endif()

pulp_add_plugin(Chorus
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Chorus"
    BUNDLE_ID         "com.pulp.examples.chorus"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.5"
    CATEGORY          Effect
    PLUGIN_CODE       "PCho"
    MANUFACTURER_CODE "PlpX"
)
