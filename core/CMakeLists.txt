find_package(DCMTK REQUIRED)

add_library(hw_core STATIC
    src/image_frame.cpp
    src/series_index.cpp
)
target_include_directories(hw_core PUBLIC include)
if(TARGET DCMTK::DCMTK)
    target_link_libraries(hw_core PUBLIC DCMTK::DCMTK)
else()
    target_link_libraries(hw_core PUBLIC ${DCMTK_LIBRARIES})
endif()

add_executable(hw_measure tools/measure.cpp)
target_link_libraries(hw_measure PRIVATE hw_core)

add_executable(hw_test_image_frame tests/test_image_frame.cpp)
target_link_libraries(hw_test_image_frame PRIVATE hw_core)
add_test(NAME image_frame COMMAND hw_test_image_frame)
