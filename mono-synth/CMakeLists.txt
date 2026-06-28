# MonoSynth — a minimal monophonic instrument. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(mono-synth-test test_mono_synth.cpp)
    target_link_libraries(mono-synth-test PRIVATE pulp::format pulp::signal Catch2::Catch2WithMain)
    target_include_directories(mono-synth-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(mono-synth-test)
endif()

pulp_add_plugin(MonoSynth
    FORMATS           CLAP Standalone
    PLUGIN_NAME       "MonoSynth"
    BUNDLE_ID         "com.pulp.examples.mono-synth"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.1.0"
    CATEGORY          Instrument
    PLUGIN_CODE       "PMsy"
    MANUFACTURER_CODE "PlpX"
)
