# 更新顶层CMakeLists.txt以包含新的示例

# 查找当前目录下所有的子目录
# GLOB_RECURSE 会查找所有匹配项，但我们使用 GLOB 来只查找一层
file(GLOB example_dirs LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*")

# 遍历找到的每一个子目录
foreach(example_dir ${example_dirs})
    # 检查这个子目录是否真的包含一个 CMakeLists.txt 文件
    # 这样可以避免包含一些非项目目录（如 __pycache__ 等）
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${example_dir}/CMakeLists.txt")
        # 为每个示例子目录调用 add_subdirectory
        # CMake 会接着去处理那个子目录里的 CMakeLists.txt 文件
        add_subdirectory(${example_dir})
    endif()
endforeach()