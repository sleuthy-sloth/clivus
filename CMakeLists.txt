cmake_minimum_required(VERSION 3.25)
project(clivus CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

option(CLIVUS_BUILD_APP "Build the Qt GUI (needs Qt6 Widgets + OpenGLWidgets)" ON)

add_subdirectory(core)
if(CLIVUS_BUILD_APP)
    add_subdirectory(app)
endif()
