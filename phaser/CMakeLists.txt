# Phaser — cascaded swept all-pass with feedback. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(phaser-test test_phaser.cpp)
    target_link_libraries(phaser-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(phaser-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(phaser-test)
endif()

pulp_add_plugin(Phaser
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Phaser"
    BUNDLE_ID         "com.pulp.examples.phaser"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.4"
    CATEGORY          Effect
    PLUGIN_CODE       "PPhs"
    MANUFACTURER_CODE "PlpX"
)
