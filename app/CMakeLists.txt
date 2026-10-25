find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Concurrent Network)
# The canvas uses fixed-function GL (glBegin/glTexCoord2f/glOrtho). macOS gets those from the
# OpenGL framework Qt already pulls in; Windows needs opengl32.lib explicitly.
find_package(OpenGL REQUIRED)

qt_standard_project_setup()

qt_add_executable(clivus
    main.cpp
    main_window.cpp deid_dialog.cpp export_images.cpp browser_model.cpp
    mpr_view.cpp
    annotation_store.cpp
    deid_dialog.cpp
    export_images.cpp
    server_dialog.cpp
    dicomweb_client.cpp
    series_summary.cpp
    branding.cpp
    about_dialog.cpp
    canvas_widget.cpp
    help_window.cpp
    style.cpp
    view_controls.cpp
)
target_link_libraries(clivus PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent Qt6::Network OpenGL::GL)
set_target_properties(clivus PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE OFF)
# The exe icon Windows shows before the app runs (Explorer, pinned taskbar): an RC resource, which
# only MSVC can consume. The running window's icon comes from setWindowIcon instead.
if(WIN32 AND MSVC)
    target_sources(clivus PRIVATE clivus.rc)
endif()

# Roadmap 3.1 offscreen roundtrip harness: QApplication + QWidget only (the canvas never paints),
# so it runs headlessly under ctest on any CI box.
qt_add_executable(clivus_test_settings_roundtrip test_settings_roundtrip.cpp main_window.cpp mpr_view.cpp annotation_store.cpp browser_model.cpp deid_dialog.cpp export_images.cpp server_dialog.cpp dicomweb_client.cpp series_summary.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
# canvas_widget.cpp's fixed-function GL calls need the GL import library explicitly on Windows
# (linking OpenGL::GL — the same as the main clivus target); Linux resolves via Qt6::OpenGLWidgets.
target_link_libraries(clivus_test_settings_roundtrip PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent Qt6::Network OpenGL::GL)
add_test(NAME settings_roundtrip COMMAND clivus_test_settings_roundtrip)
set_tests_properties(settings_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Roadmap 3.2 offscreen filter harness: drives the real scan path with the synthetic corpus and
# then exercises the search box like a user would.
qt_add_executable(clivus_test_series_filter test_series_filter.cpp main_window.cpp mpr_view.cpp annotation_store.cpp browser_model.cpp deid_dialog.cpp export_images.cpp server_dialog.cpp dicomweb_client.cpp series_summary.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_series_filter PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent Qt6::Network OpenGL::GL)
add_test(NAME series_filter COMMAND clivus_test_series_filter "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(series_filter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Roadmap 3.5 offscreen error-recovery harness: drives the real scan/decode paths against an empty
# folder and deliberately corrupted copies, and asserts readable failures, no stuck indicators, and
# recovery without restart.
qt_add_executable(clivus_test_error_paths test_error_paths.cpp main_window.cpp mpr_view.cpp annotation_store.cpp browser_model.cpp deid_dialog.cpp export_images.cpp server_dialog.cpp dicomweb_client.cpp series_summary.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_error_paths PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent Qt6::Network OpenGL::GL)
add_test(NAME error_paths COMMAND clivus_test_error_paths "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")

qt_add_executable(clivus_test_measurements test_measurements.cpp main_window.cpp mpr_view.cpp annotation_store.cpp browser_model.cpp deid_dialog.cpp export_images.cpp server_dialog.cpp dicomweb_client.cpp series_summary.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_measurements PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent Qt6::Network OpenGL::GL)
add_test(NAME measurements COMMAND clivus_test_measurements "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(measurements PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(clivus_test_mpr test_mpr.cpp main_window.cpp mpr_view.cpp annotation_store.cpp browser_model.cpp deid_dialog.cpp export_images.cpp server_dialog.cpp dicomweb_client.cpp series_summary.cpp branding.cpp about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_mpr PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent Qt6::Network OpenGL::GL)
add_test(NAME mpr COMMAND clivus_test_mpr "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/volume" "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus/JPEG2000.dcm")
set_tests_properties(mpr PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The manual alone: no canvas, no GL context needed, so this runs anywhere.
qt_add_executable(clivus_test_help_search test_help_search.cpp help_window.cpp branding.cpp style.cpp)
target_link_libraries(clivus_test_help_search PRIVATE clivus_core Qt6::Widgets)
add_test(NAME help_search COMMAND clivus_test_help_search)
set_tests_properties(help_search PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(clivus_test_series_summary test_series_summary.cpp series_summary.cpp)
target_link_libraries(clivus_test_series_summary PRIVATE clivus_core Qt6::Gui)
add_test(NAME series_summary COMMAND clivus_test_series_summary)

# Overlay text: paints the canvas overlay code into a QImage, so contrast and label clamping are
# checked without a GL context. Only the canvas and the palette are needed.
qt_add_executable(clivus_test_overlay_render test_overlay_render.cpp canvas_widget.cpp style.cpp)
target_link_libraries(clivus_test_overlay_render PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets
                                                        OpenGL::GL)
add_test(NAME overlay_render COMMAND clivus_test_overlay_render)
set_tests_properties(overlay_render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# View state: what a slice step keeps and what a series switch resets.
qt_add_executable(clivus_test_canvas_view test_canvas_view.cpp canvas_widget.cpp style.cpp)
target_link_libraries(clivus_test_canvas_view PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets
                                                     OpenGL::GL)
add_test(NAME canvas_view COMMAND clivus_test_canvas_view)
set_tests_properties(canvas_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Browser hierarchy: pure grouping and labelling, no widgets, no window.
qt_add_executable(clivus_test_browser_model test_browser_model.cpp browser_model.cpp
                                                     series_summary.cpp)
target_link_libraries(clivus_test_browser_model PRIVATE clivus_core Qt6::Gui)
add_test(NAME browser_model COMMAND clivus_test_browser_model)

# DICOMweb client: Qt6::Network is the only addition; the test runs a mock HTTP server in-process.
qt_add_executable(clivus_test_dicomweb test_dicomweb.cpp dicomweb_client.cpp)
target_link_libraries(clivus_test_dicomweb PRIVATE clivus_core Qt6::Core Qt6::Network)
qt_add_executable(clivus_test_annotations test_annotations.cpp main_window.cpp deid_dialog.cpp export_images.cpp mpr_view.cpp
    annotation_store.cpp browser_model.cpp deid_dialog.cpp export_images.cpp server_dialog.cpp dicomweb_client.cpp series_summary.cpp branding.cpp
    about_dialog.cpp help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_annotations PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets
    Qt6::Concurrent Qt6::Network OpenGL::GL)
add_test(NAME annotations COMMAND clivus_test_annotations "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(annotations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(clivus_test_cine test_cine.cpp main_window.cpp deid_dialog.cpp export_images.cpp mpr_view.cpp annotation_store.cpp browser_model.cpp
    server_dialog.cpp dicomweb_client.cpp series_summary.cpp branding.cpp about_dialog.cpp
    help_window.cpp canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_cine PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets
    Qt6::Concurrent Qt6::Network OpenGL::GL)
add_test(NAME cine COMMAND clivus_test_cine "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(cine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(clivus_test_export test_export.cpp deid_dialog.cpp export_images.cpp)
target_link_libraries(clivus_test_export PRIVATE clivus_core Qt6::Widgets Qt6::Concurrent)
add_test(NAME export COMMAND clivus_test_export "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(export PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# v0.4.1 browser groups: folding and unfolding a patient or study, and everything that has to keep
# working around it (selection, scan batches, the query, the empty folder).
qt_add_executable(clivus_test_browser_groups test_browser_groups.cpp main_window.cpp browser_model.cpp
    series_summary.cpp deid_dialog.cpp export_images.cpp mpr_view.cpp annotation_store.cpp
    server_dialog.cpp dicomweb_client.cpp branding.cpp about_dialog.cpp help_window.cpp
    canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_browser_groups PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets
    Qt6::Concurrent Qt6::Network OpenGL::GL)
add_test(NAME browser_groups COMMAND clivus_test_browser_groups "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(browser_groups PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# v0.4.1 overlay layout: the corner blocks get half the canvas each and are laid out once, in the
# rectangle they are drawn into, so a long description cannot run under the window/level block.
qt_add_executable(clivus_test_overlay_layout test_overlay_layout.cpp canvas_widget.cpp style.cpp)
target_link_libraries(clivus_test_overlay_layout PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets
    OpenGL::GL)
add_test(NAME overlay_layout COMMAND clivus_test_overlay_layout)
set_tests_properties(overlay_layout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# v0.4.1 scroll scheduling: burst, prefetch overlap, direction change and a sequential walk, with the
# request -> present numbers printed so the report can quote them instead of an impression.
qt_add_executable(clivus_test_scroll test_scroll.cpp main_window.cpp browser_model.cpp
    series_summary.cpp deid_dialog.cpp export_images.cpp mpr_view.cpp annotation_store.cpp
    server_dialog.cpp dicomweb_client.cpp branding.cpp about_dialog.cpp help_window.cpp
    canvas_widget.cpp view_controls.cpp)
target_link_libraries(clivus_test_scroll PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets
    Qt6::Concurrent Qt6::Network OpenGL::GL)
add_test(NAME scroll COMMAND clivus_test_scroll "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(scroll PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)

add_test(NAME dicomweb COMMAND clivus_test_dicomweb "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")

qt_add_executable(clivus_test_server_dialog test_server_dialog.cpp server_dialog.cpp dicomweb_client.cpp)
target_link_libraries(clivus_test_server_dialog PRIVATE clivus_core Qt6::Widgets Qt6::Network)
add_test(NAME server_dialog COMMAND clivus_test_server_dialog "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")
set_tests_properties(server_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
