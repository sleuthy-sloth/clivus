find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Concurrent)
# The canvas uses fixed-function GL (glBegin/glTexCoord2f/glOrtho). macOS gets those from the
# OpenGL framework Qt already pulls in; Windows needs opengl32.lib explicitly.
find_package(OpenGL REQUIRED)

qt_standard_project_setup()

qt_add_executable(clivus
    main.cpp
    main_window.cpp
    canvas_widget.cpp
    style.cpp
)
target_link_libraries(clivus PRIVATE clivus_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent OpenGL::GL)
set_target_properties(clivus PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE OFF)

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
