add_executable(async_client 
    client.h    
    main.cpp)

# 链接到 cxpnet 库，自动获得 C++20 和 include 路径
target_link_libraries(async_client PRIVATE cxpnet::cxpnet)