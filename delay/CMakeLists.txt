# Delay — feedback delay line with wet/dry mix. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(delay-test test_delay.cpp)
    target_link_libraries(delay-test PRIVATE pulp::format Catch2::Catch2WithMain)
    target_include_directories(delay-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(delay-test)

    # Editor render test — needs pulp::view (Ink & Signal + Skia screenshot).
    # Skips gracefully when the Skia raster backend isn't in the SDK build.
    if(TARGET pulp::view)
        add_executable(delay-editor-test test_delay_editor.cpp)
        target_link_libraries(delay-editor-test
            PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
        target_include_directories(delay-editor-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        catch_discover_tests(delay-editor-test)
    endif()
endif()

pulp_add_plugin(Delay
    FORMATS           VST3 CLAP Standalone
    PLUGIN_NAME       "Delay"
    BUNDLE_ID         "com.pulp.examples.delay"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.1.0"
    CATEGORY          Effect
    PLUGIN_CODE       "PDly"
    MANUFACTURER_CODE "PlpX"
)
