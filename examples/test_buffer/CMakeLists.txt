add_executable(test_buffer main.cpp)

# 链接到 cxpnet 库，自动获得 C++20 和 include 路径
target_link_libraries(test_buffer PRIVATE cxpnet::cxpnet)

#target_compile_definitions(test_buffer PRIVATE NDEBUG)