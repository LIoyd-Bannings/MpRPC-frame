#pragma once
#include <google/protobuf/service.h>
#include <functional>

//自己定义的Closure 完美支持C++ lambda
class MprpcClosure:public google::protobuf::Closure{
public:
//构造函数 接受一个lambda表达式
    MprpcClosure(std::function<void()>cb):cb_(cb){}
 
    //重写Run（）方法  这是网络回包后，底层 CallMethod 会调用的函数
    void Run()override
    {
        cb_();          // 1. 执行业务层传入的 Lambda 逻辑
        delete this;    // 2. 极其重要：执行完后，自己杀掉自己，释放闭包内存！
    }


private:
    std::function<void()>cb_;//把Lambda封起来
};