# MIDI Transpose — semitone note shifter. See README.md.

if(PULP_BUILD_TESTS)
    add_executable(midi-transpose-test test_midi_transpose.cpp)
    target_link_libraries(midi-transpose-test PRIVATE pulp::format Catch2::Catch2WithMain)
    target_include_directories(midi-transpose-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(midi-transpose-test)
endif()

# A pure MIDI effect — CLAP carries MIDI cleanly; standalone for quick checks.
pulp_add_plugin(MidiTranspose
    FORMATS           CLAP Standalone
    PLUGIN_NAME       "MidiTranspose"
    BUNDLE_ID         "com.pulp.examples.midi-transpose"
    MANUFACTURER      "Pulp Examples"
    VERSION           "0.1.0"
    CATEGORY          MidiEffect
    PLUGIN_CODE       "PMtr"
    MANUFACTURER_CODE "PlpX"
)
