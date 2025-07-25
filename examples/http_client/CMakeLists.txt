add_executable(http_client main.cpp)

# 链接到 cxpnet 库，自动获得 C++20 和 include 路径
target_link_libraries(http_client PRIVATE cxpnet::cxpnet)