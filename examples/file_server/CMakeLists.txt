add_executable(file_server main.cpp)
target_link_libraries(file_server PRIVATE cxpnet::cxpnet)
