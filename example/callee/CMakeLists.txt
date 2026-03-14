#set(SRC_LIST userservice.cpp ../user.pb.cc)
#

include_directories(../db)

set(SRC_LIST agentserver.cpp ../agent.pb.cc ../db/db.cpp )

add_executable(agent_provider ${SRC_LIST})

target_link_libraries(agent_provider mprpc protobuf mysqlclient)