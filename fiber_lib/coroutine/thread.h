#ifndef _THREAD_H_
#define _THREAD_H_

#include <mutex>
#include <condition_variable>
#include <functional>     

namespace sylar
{

// 用于线程方法间的同步
class Semaphore 
{
public:
    // 信号量初始化为0
    explicit Semaphore(int count = 0) : count_(count) {}
    
    // P操作
    void wait() 
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return count_ > 0; });
        --count_;
    }

    // V操作
    void signal() 
    {
        std::unique_lock<std::mutex> lock(mtx_);
        ++count_;
        cv_.notify_one();  // signal
    }

private:
    std::mutex mtx_;                
    std::condition_variable cv_;    
    int count_; 
};

// 一共两种线程: 1、由系统自动创建的主线程；2、由Thread类创建的线程 
class Thread 
{
public:
    Thread(std::function<void()> cb, const std::string& name);
    ~Thread();

    pid_t getId() const { return id_; }
    const std::string& getName() const { return name_; }

    void join();

public:
    // 获取系统分配的线程id
	static pid_t GetThreadId();
    // 获取当前所在线程
    static Thread* GetThis();

    // 获取当前线程的名字
    static const std::string& GetName();
    // 设置当前线程的名字
    static void SetName(const std::string& name);

private:
	// 线程函数，尝试深入理解run函数设置为private、static的原因
    static void* run(void* arg);

private:
    pid_t id_ = -1;
    pthread_t thread_ = 0;

    // 线程需要运行的函数
    std::function<void()> cb_;
    std::string name_;
    
    Semaphore semaphore_;
};

}  // namespace sylar

#endif