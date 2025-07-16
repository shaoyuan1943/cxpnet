# cxpnet/examples/example_basic/CMakeLists.txt

# 为这个示例创建一个可执行文件
# 目标名称最好能反映出它的身份，比如加上 "example_" 前缀
add_executable(example_basic main.cpp)

# 链接到 cxpnet 库，自动获得 C++20 和 include 路径
target_link_libraries(example_basic PRIVATE cxpnet::cxpnet)

#add_executable(example_network_client client.cpp)
#target_link_libraries(example_network_client PRIVATE cxpnet::cxpnet)

#add_executable(example_network_server server.cpp)
#target_link_libraries(example_network_server PRIVATE cxpnet::cxpnet)