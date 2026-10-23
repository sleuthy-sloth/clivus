find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Concurrent)
# The canvas uses fixed-function GL (glBegin/glTexCoord2f/glOrtho). macOS gets those from the
# OpenGL framework Qt already pulls in; Windows needs opengl32.lib explicitly.
find_package(OpenGL REQUIRED)

qt_standard_project_setup()

qt_add_executable(clivus
    main.cpp
    main_window.cpp
    mpr_view.cpp
    branding.cpp
    about_dialog.cpp
    canvas_widget.cpp
    help_window.cpp
    style.cpp
    view_controls.cpp
)
target_link_libraries(clivus PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent OpenGL::GL)
set_target_properties(clivus PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE OFF)

# Roadmap 3.1 offscreen roundtrip harness: QApplication + QWidget only (the canvas never paints),
# so it runs headlessly under ctest on any CI box.
qt_add_executable(clivus_test_settings_roundtrip test_settings_roundtrip.cpp main_window.cpp mpr_view.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
# canvas_widget.cpp's fixed-function GL calls need the GL import library explicitly on Windows
# (linking OpenGL::GL — the same as the main clivus target); Linux resolves via Qt6::OpenGLWidgets.
target_link_libraries(clivus_test_settings_roundtrip PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent OpenGL::GL)
add_test(NAME settings_roundtrip COMMAND clivus_test_settings_roundtrip)
set_tests_properties(settings_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Roadmap 3.2 offscreen filter harness: drives the real scan path with the synthetic corpus and
# then exercises the search box like a user would.
qt_add_executable(clivus_test_series_filter test_series_filter.cpp main_window.cpp mpr_view.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_series_filter PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent OpenGL::GL)
add_test(NAME series_filter COMMAND clivus_test_series_filter "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(series_filter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Roadmap 3.5 offscreen error-recovery harness: drives the real scan/decode paths against an empty
# folder and deliberately corrupted copies, and asserts readable failures, no stuck indicators, and
# recovery without restart.
qt_add_executable(clivus_test_error_paths test_error_paths.cpp main_window.cpp mpr_view.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_error_paths PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent OpenGL::GL)
add_test(NAME error_paths COMMAND clivus_test_error_paths "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")

qt_add_executable(clivus_test_measurements test_measurements.cpp main_window.cpp mpr_view.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_measurements PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent OpenGL::GL)
add_test(NAME measurements COMMAND clivus_test_measurements "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(measurements PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(clivus_test_mpr test_mpr.cpp main_window.cpp mpr_view.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_mpr PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent OpenGL::GL)
add_test(NAME mpr COMMAND clivus_test_mpr "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/volume" "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus/JPEG2000.dcm")
set_tests_properties(mpr PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(error_paths PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# VTK probe: off by default. It needs VTK 9 with the volume rendering modules and no Qt, so it can run
# on any box where VTK is installed, including the Windows artifact builder.
option(CLIVUS_BUILD_VTK_PROBE "Build the headless VTK volume-rendering probe" OFF)
if(CLIVUS_BUILD_VTK_PROBE)
    find_package(VTK 9.1 REQUIRED COMPONENTS
        CommonCore CommonDataModel ImagingCore RenderingCore RenderingOpenGL2
        RenderingVolumeOpenGL2 RenderingFreeType InteractionStyle IOImage)
    add_executable(vtk_probe vtk_probe.cpp)
    target_link_libraries(vtk_probe PRIVATE clivus_core ${VTK_LIBRARIES})
    vtk_module_autoinit(TARGETS vtk_probe MODULES ${VTK_LIBRARIES})
endif()

# The source carries non-ASCII literals (middle dots, dashes, degree signs) as UTF-8 bytes; without /utf-8 MSVC reads them as cp1252 and ships mojibake on Windows.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_compile_definitions(CLIVUS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
