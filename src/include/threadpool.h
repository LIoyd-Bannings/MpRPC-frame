#pragma once
#include<thread>
#include<vector>
#include<queue>
#include<mutex>
#include<functional>
#include<condition_variable>
#include <future>
class ThreadPool
{
public:
    ThreadPool(int employees_num):stop(false)
    {
        for(int i=0;i<employees_num;++i)
        {
            workers.emplace_back([this]{
                
                while(true)
                {
                    std::function<void()>task;//准备接任务
                    {
                        std::unique_lock<std::mutex>lock(this->que_mtx);
                        this->condition.wait(lock,[this]{return this->stop||!this->tasks.empty();});

                        if(this->stop&&this->tasks.empty())
                        {
                            return;
                        }
                        task=std::move(tasks.front());
                        this->tasks.pop();
                        task();
                    }
                }


            });
        }
    }
    ~ThreadPool()
    {
        { 
            std::unique_lock<std::mutex>lock(que_mtx);
            stop=true;
        }
        condition.notify_all();
        for(std::thread& worker:workers)
        {
            worker.join();
        }
    }
template<class F,class ... Args>
void addTask(F&& f,Args&&...args)
{   
    //把函数和参数打包成一个“任务包”
    //比如：Login(req, resp) 打包成一个 void() 类型的函数对象
    using ReturnType=typename std::result_of<F(Args...)>::type;
    auto task=std::make_shared<std::packaged_task<ReturnType()>>(std::bind(std::forward<F>(f),std::forward<Args>(args)...));

    //把这个包转换为void()类型，方便放入通用队列
    std::function<void()>wrapper_func=[task](){(*task)();};
    {
        //加锁放任务
        std::unique_lock<std::mutex>lcok(que_mtx);
        //不允许停业后接单
        if(stop)throw std::runtime_error("enqueue on stopped ThreadPool");
        tasks.emplace(wrapper_func);//扔进篮子
    }
    //通知 唤醒一个线程干活
    condition.notify_one();
}

private:
    std::vector<std::thread>workers;//工作线程
    std::queue<std::function<void()>>tasks;//任务队列
    std::mutex que_mtx;//队列锁
    std::condition_variable condition;
    bool stop;//停止标志
};