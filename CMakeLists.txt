cmake_minimum_required(VERSION 3.24)
project(pulp-classic-effects LANGUAGES CXX)

# Consume the Pulp SDK. The simplest path is to scaffold against an existing
# Pulp checkout/install with `pulp create`, which pins the SDK for you. To wire
# this repo directly, point CMake at a Pulp build or install tree, e.g.:
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH=/path/to/pulp/install
#
# find_package(Pulp) provides pulp::format, pulp::signal, the pulp_add_plugin()
# helper, and a bundled Catch2 for the tests.
find_package(Pulp CONFIG REQUIRED)

option(PULP_BUILD_TESTS "Build the example tests" ON)
enable_testing()

add_subdirectory(tremolo)
add_subdirectory(ring-mod)
add_subdirectory(delay)
add_subdirectory(vibrato)
add_subdirectory(chorus)
add_subdirectory(compressor-expander)
add_subdirectory(parametric-eq)
add_subdirectory(wah)
add_subdirectory(pitch-shift)
add_subdirectory(midi-transpose)
add_subdirectory(mono-synth)
add_subdirectory(gui-zoo)

# Combined render check for every effect's dark Ink & Signal editor. Needs
# pulp::view (Ink & Signal theme + Skia screenshot); skips gracefully at runtime
# when the Skia raster backend isn't compiled into the SDK build.
if(PULP_BUILD_TESTS AND TARGET pulp::view)
    add_executable(editors-test test_editors.cpp)
    target_link_libraries(editors-test PRIVATE pulp::format pulp::view Catch2::Catch2WithMain)
    target_include_directories(editors-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/tremolo
        ${CMAKE_CURRENT_SOURCE_DIR}/ring-mod
        ${CMAKE_CURRENT_SOURCE_DIR}/delay
        ${CMAKE_CURRENT_SOURCE_DIR}/vibrato
        ${CMAKE_CURRENT_SOURCE_DIR}/chorus
        ${CMAKE_CURRENT_SOURCE_DIR}/compressor-expander
        ${CMAKE_CURRENT_SOURCE_DIR}/parametric-eq
        ${CMAKE_CURRENT_SOURCE_DIR}/wah
        ${CMAKE_CURRENT_SOURCE_DIR}/pitch-shift)
    catch_discover_tests(editors-test)
endif()
