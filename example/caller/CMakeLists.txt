#set(SRC_LIST calluserservice.cpp  ../user.pb.cc)
#set(SRC_LIST callfriendservice.cpp  ../friend.pb.cc)
#add_executable(consumer  ${SRC_LIST})

#target_link_libraries(consumer mprpc protobuf)

set(SRC_LIST callagentservice.cpp ../agent.pb.cc)
add_executable(consumer ${SRC_LIST})
target_link_libraries(consumer mprpc protobuf)
add_executable(test_llm test_llm.cpp)
# 链接 ssl 和 crypto，打通 HTTPS 
target_link_libraries(test_llm ssl crypto pthread)
add_executable(agent_gateway agent_gateway.cpp ../agent.pb.cc)
target_link_libraries(agent_gateway mprpc protobuf ssl crypto pthread)