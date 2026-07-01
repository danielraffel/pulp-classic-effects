# Compressor / Expander — stereo-linked dynamics processor. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(compressor-expander-test test_compressor_expander.cpp)
    target_link_libraries(compressor-expander-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(compressor-expander-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(compressor-expander-test)
endif()

pulp_add_plugin(CompressorExpander
    FORMATS           VST3 AU CLAP
    PLUGIN_NAME       "Compressor"
    BUNDLE_ID         "com.pulp.examples.compressor-expander"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.2.2"
    CATEGORY          Effect
    PLUGIN_CODE       "PCxE"
    MANUFACTURER_CODE "PlpX"
)
