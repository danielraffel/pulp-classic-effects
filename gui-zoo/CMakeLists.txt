# gui-zoo — UI fixture with a deterministic screenshot baseline.
#
# Not a plugin: a View-tree fixture rendered headlessly via the Skia raster
# backend and compared against a checked-in baseline (baseline.png). The test
# only runs where the view + Skia raster backend are available; it SKIPs
# cleanly otherwise (e.g. a build without Skia).

if(PULP_BUILD_TESTS)
    add_executable(gui-zoo-test test_gui_zoo.cpp)
    target_link_libraries(gui-zoo-test PRIVATE pulp::view Catch2::Catch2WithMain)
    target_include_directories(gui-zoo-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(gui-zoo-test PRIVATE GUI_ZOO_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    catch_discover_tests(gui-zoo-test)
endif()
