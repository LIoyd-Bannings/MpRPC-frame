#set(SRC_LIST userservice.cpp ../user.pb.cc)
#

# include_directories(../db)

# set(SRC_LIST agentserver.cpp ../agent.pb.cc ../db/db.cpp )

# add_executable(agent_provider ${SRC_LIST})

# target_link_libraries(agent_provider mprpc protobuf mysqlclient)


include_directories(../db)

set(SRC_LIST agentserver.cpp ../agent.pb.cc ../db/db.cpp ../db/ConnectionPool.cpp)

add_executable(agent_provider ${SRC_LIST})

# 🌟 终极护甲穿透：补上 zmq, zookeeper_mt 和 pthread！
target_link_libraries(agent_provider 
    mprpc 
    protobuf 
    mysqlclient 
    zmq 
    zookeeper_mt 
    pthread
)