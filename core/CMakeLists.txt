find_package(DCMTK REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(clivus_core STATIC
    src/codecs.cpp
    src/image_frame.cpp
    src/index_store.cpp
    src/manual_search.cpp
    src/volume.cpp
    src/series_index.cpp
    src/roi.cpp
)
target_include_directories(clivus_core PUBLIC include)
target_link_libraries(clivus_core PUBLIC clivus_version)
if(TARGET DCMTK::DCMTK)
    target_link_libraries(clivus_core PUBLIC DCMTK::DCMTK)
else()
    target_link_libraries(clivus_core PUBLIC ${DCMTK_LIBRARIES})
endif()

target_link_libraries(clivus_core PRIVATE SQLite::SQLite3)

add_executable(clivus_measure tools/measure.cpp)
target_link_libraries(clivus_measure PRIVATE clivus_core)

add_executable(clivus_parity tools/parity.cpp)
target_link_libraries(clivus_parity PRIVATE clivus_core)

# Optional parity test: skips loudly when testdata/corpus is absent or no argument.
add_test(NAME parity COMMAND clivus_parity
    "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus"
    "--threshold" "0")

add_executable(clivus_test_image_frame tests/test_image_frame.cpp)
target_link_libraries(clivus_test_image_frame PRIVATE clivus_core)
add_test(NAME image_frame COMMAND clivus_test_image_frame)

# Needs testdata/corpus; skips itself when that folder is absent (it is regenerated, not committed).
add_executable(clivus_test_decode tests/test_decode.cpp)
target_link_libraries(clivus_test_decode PRIVATE clivus_core)
add_test(NAME decode_matrix COMMAND clivus_test_decode "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")

# Needs testdata/volume (committed, 4x16x16 slices with a value that names its own slice index).
add_executable(clivus_test_volume tests/test_volume.cpp)
target_link_libraries(clivus_test_volume PRIVATE clivus_core)
add_test(NAME volume COMMAND clivus_test_volume "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/volume")

add_executable(clivus_test_index_store tests/test_index_store.cpp)
target_link_libraries(clivus_test_index_store PRIVATE clivus_core)
add_test(NAME index_store COMMAND clivus_test_index_store)

add_executable(clivus_test_manual_search tests/test_manual_search.cpp)
target_link_libraries(clivus_test_manual_search PRIVATE clivus_core)
add_test(NAME manual_search COMMAND clivus_test_manual_search)

# Needs testdata/corpus: the scan is only interesting when there are objects to walk over.
add_executable(clivus_test_roi tests/test_roi.cpp)
target_link_libraries(clivus_test_roi PRIVATE clivus_core)
add_test(NAME roi COMMAND clivus_test_roi)

add_executable(clivus_test_scan tests/test_scan.cpp)
target_link_libraries(clivus_test_scan PRIVATE clivus_core)
add_test(NAME scan COMMAND clivus_test_scan "${CMAKE_CURRENT_SOURCE_DIR}/../testdata/corpus")

# Writes its own two objects (MONOCHROME1 and MONOCHROME2 over the same pixels), so no corpus needed.
add_executable(clivus_test_photometric tests/test_photometric.cpp)
target_link_libraries(clivus_test_photometric PRIVATE clivus_core)
add_test(NAME photometric COMMAND clivus_test_photometric)

# Writes its own three objects (RGB interleaved, RGB planar, YBR_FULL) over the same pixels, so no
# corpus needed.
add_executable(clivus_test_colour tests/test_colour.cpp)
target_link_libraries(clivus_test_colour PRIVATE clivus_core)
add_test(NAME colour COMMAND clivus_test_colour)

# The tests are assert-based, and Release would define NDEBUG and silently erase every check while
# still printing "all checks passed". Strip it for test targets only.
foreach(test_target clivus_test_image_frame clivus_test_decode clivus_test_volume
                     clivus_test_index_store clivus_test_photometric clivus_test_colour)
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
endforeach()

# The source carries non-ASCII literals (middle dots, dashes, degree signs) as UTF-8 bytes; without /utf-8 MSVC reads them as cp1252 and ships mojibake on Windows.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
