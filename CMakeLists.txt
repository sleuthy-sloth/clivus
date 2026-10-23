cmake_minimum_required(VERSION 3.25)
# The one place the version lives: vcpkg.json does not repeat it, and the GUI reports this header
# instead of its own string.
project(clivus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

option(CLIVUS_BUILD_APP "Build the Qt GUI (needs Qt6 Widgets + OpenGLWidgets)" ON)

configure_file(cmake/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/clivus/version.h @ONLY)
add_library(clivus_version INTERFACE)
target_include_directories(clivus_version INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_subdirectory(core)
if(CLIVUS_BUILD_APP)
    add_subdirectory(app)
endif()

# The source carries non-ASCII literals (middle dots, dashes, degree signs) as UTF-8 bytes; without /utf-8 MSVC reads them as cp1252 and ships mojibake on Windows.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
