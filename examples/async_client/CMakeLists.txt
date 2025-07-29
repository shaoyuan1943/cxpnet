# ./examples/async_client/CMakeLists.txt
add_executable(async_client main.cpp)
target_link_libraries(async_client PRIVATE cxpnet::cxpnet)