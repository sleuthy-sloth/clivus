find_package(DCMTK REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(hw_core STATIC
    src/codecs.cpp
    src/image_frame.cpp
    src/index_store.cpp
    src/volume.cpp
    src/series_index.cpp
)
target_include_directories(hw_core PUBLIC include)
if(TARGET DCMTK::DCMTK)
    target_link_libraries(hw_core PUBLIC DCMTK::DCMTK)
else()
    target_link_libraries(hw_core PUBLIC ${DCMTK_LIBRARIES})
endif()

target_link_libraries(hw_core PRIVATE SQLite::SQLite3)

add_executable(hw_measure tools/measure.cpp)
target_link_libraries(hw_measure PRIVATE hw_core)

add_executable(hw_test_image_frame tests/test_image_frame.cpp)
target_link_libraries(hw_test_image_frame PRIVATE hw_core)
add_test(NAME image_frame COMMAND hw_test_image_frame)

# Needs testdata/corpus; skips itself when that folder is absent (it is regenerated, not committed).
add_executable(hw_test_decode tests/test_decode.cpp)
target_link_libraries(hw_test_decode PRIVATE hw_core)
add_test(NAME decode_matrix COMMAND hw_test_decode "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")

# Needs testdata/volume (committed, 4x16x16 slices with a value that names its own slice index).
add_executable(hw_test_volume tests/test_volume.cpp)
target_link_libraries(hw_test_volume PRIVATE hw_core)
add_test(NAME volume COMMAND hw_test_volume "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/volume")

add_executable(hw_test_index_store tests/test_index_store.cpp)
target_link_libraries(hw_test_index_store PRIVATE hw_core)
add_test(NAME index_store COMMAND hw_test_index_store)

# The tests are assert-based, and Release would define NDEBUG and silently erase every check while
# still printing "all checks passed". Strip it for test targets only.
foreach(test_target hw_test_image_frame hw_test_decode hw_test_volume hw_test_index_store)
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
endforeach()
