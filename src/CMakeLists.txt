#aux_source_directory(. SRC_LIST)
# set(SRC_LIST mprpcapplication.cpp mprpcconfig.cpp rpcheader.pb.cc mcp.pb.cc
#  rpcprovider.cpp mprpcchannel.cpp mprpccontroller.cpp logger.cpp zookeeperutil.cpp) 
# add_library(mprpc  ${SRC_LIST})
# target_link_libraries(mprpc  muduo_net muduo_base   pthread zookeeper_mt) 
# src/CMakeLists.txt
# aux_source_directory(. SRC_LIST)
set(SRC_LIST mprpcapplication.cpp mprpcconfig.cpp rpcheader.pb.cc mcp.pb.cc rpcprovider.cpp mprpcchannel.cpp mprpccontroller.cpp logger.cpp zookeeperutil.cpp) 

add_library(mprpc ${SRC_LIST})

# 彻底删除 muduo_net 和 muduo_base，强力注入 zmq
target_link_libraries(mprpc zmq pthread zookeeper_mt)