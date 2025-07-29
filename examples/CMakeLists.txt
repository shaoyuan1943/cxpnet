# ./examples/CMakeLists.txt
file(GLOB example_subdirs LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*")
foreach(subdir ${example_subdirs})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/CMakeLists.txt")
        add_subdirectory(${subdir})
    endif()
endforeach()