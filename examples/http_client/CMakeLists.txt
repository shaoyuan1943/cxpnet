add_executable(http_client main.cpp)
target_link_libraries(http_client PRIVATE cxpnet::cxpnet)
