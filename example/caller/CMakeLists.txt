#set(SRC_LIST calluserservice.cpp  ../user.pb.cc)
#set(SRC_LIST callfriendservice.cpp  ../friend.pb.cc)
#add_executable(consumer  ${SRC_LIST})

#target_link_libraries(consumer mprpc protobuf)

# set(SRC_LIST callagentservice.cpp ../agent.pb.cc)
# add_executable(consumer ${SRC_LIST})
# target_link_libraries(consumer mprpc protobuf)
# add_executable(test_llm test_llm.cpp)
# # 链接 ssl 和 crypto，打通 HTTPS 
# target_link_libraries(test_llm ssl crypto pthread)
# add_executable(agent_gateway agent_gateway.cpp ../agent.pb.cc)
# target_link_libraries(agent_gateway mprpc protobuf ssl crypto pthread)
# # 1. 加载 PkgConfig 模块
# find_package(PkgConfig REQUIRED)

# # 2. 通过 pkg-config 寻找 libzmq (ZeroMQ 的底层包名)
# pkg_check_modules(ZMQ REQUIRED libzmq)

# # 3. 包含头文件路径
# include_directories(${ZMQ_INCLUDE_DIRS})

# add_executable(chat_client chat_client.cpp)
# target_link_libraries(chat_client zmq pthread)

# # 4. 链接库文件 (注意这里的变量名)
# target_link_libraries(agent_gateway 
#     ${ZMQ_LIBRARIES}
#     mprpc 
#     protobuf 
#     zookeeper_mt 
#     pthread
# )

# example/caller/CMakeLists.txt




# ==========================================================
# 注释掉老旧的 consumer 测试代码

# ==========================================================
# 1. 编译纯 LLM 测试节点
# ==========================================================
# add_executable(test_llm test_llm.cpp)
# target_link_libraries(test_llm ssl crypto pthread)

# ==========================================================
# 2. 编译林弟 (客户端)
# ==========================================================
add_executable(chat_client chat_client.cpp)
target_link_libraries(chat_client zmq pthread)

# ==========================================================
# 3. 编译网关 (Agent Gateway)
# ==========================================================
add_executable(agent_gateway agent_gateway.cpp ../agent.pb.cc     ../db/db.cpp              #  新增
../db/ConnectionPool.cpp  )
# 将 hiredis 动态库链接到你的网关执行文件上
target_link_libraries(agent_gateway mprpc crypt pthread hiredis)
# 把底层 mprpc、大模型需要的 ssl/crypto、以及核动力 zmq 全部融为一体！
target_link_libraries(agent_gateway 
    mprpc 
    protobuf 
    zmq 
    zookeeper_mt 
    ssl 
    crypto 
    pthread
    mysqlclient
    crypt
)
