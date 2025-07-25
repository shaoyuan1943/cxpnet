add_executable(tcp_server main.cpp)

# 链接到 cxpnet 库，自动获得 C++20 和 include 路径
target_link_libraries(tcp_server PRIVATE cxpnet::cxpnet)