#set(SRC_LIST calluserservice.cpp  ../user.pb.cc)
set(SRC_LIST callfriendservice.cpp  ../friend.pb.cc)
add_executable(consumer  ${SRC_LIST})

target_link_libraries(consumer mprpc protobuf)