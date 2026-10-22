find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

qt_standard_project_setup()

qt_add_executable(horoswin
    main.cpp
    main_window.cpp
    canvas_widget.cpp
)
target_link_libraries(horoswin PRIVATE hw_core Qt6::Widgets Qt6::OpenGLWidgets)
set_target_properties(horoswin PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE OFF)
