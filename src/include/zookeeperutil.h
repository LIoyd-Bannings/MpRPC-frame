#pragma once
#include<semaphore.h>
#include<zookeeper/zookeeper.h>
#include<string>

//封装的zk客户端类
class ZkClient
{
public:
    ZkClient();
    ~ZkClient();
    void Start();

    //再zkserver上根据指定的path创建znode节点
    void Create(const char* path,const char *data ,int datalen,int state=0);
    //根据传入参数指定的znode节点的路径，获得znode节点的值
    std::string GetData(const char *path);

private:
//zk的客户端句柄
zhandle_t *m_zhandle;

};