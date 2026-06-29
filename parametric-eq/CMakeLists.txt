# Parametric EQ — three-band equalizer (low shelf / mid bell / high shelf). See README.md.

if(PULP_BUILD_TESTS)
    add_executable(parametric-eq-test test_parametric_eq.cpp)
    target_link_libraries(parametric-eq-test PRIVATE pulp::format Catch2::Catch2WithMain)
    target_include_directories(parametric-eq-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(parametric-eq-test)
endif()

pulp_add_plugin(ParametricEq
    FORMATS           VST3 CLAP Standalone
    PLUGIN_NAME       "Parametric EQ"
    BUNDLE_ID         "com.pulp.examples.parametric-eq"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.1.0"
    CATEGORY          Effect
    PLUGIN_CODE       "PPeq"
    MANUFACTURER_CODE "PlpX"
)
