add_executable(all_one_thread_server main.cpp)
target_link_libraries(all_one_thread_server PRIVATE cxpnet::cxpnet)
