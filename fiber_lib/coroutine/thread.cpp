#include "thread.h"

#include <sys/syscall.h> 
#include <iostream>
#include <unistd.h>  

namespace sylar {

// 线程信息
static thread_local Thread* t_thread          = nullptr;
static thread_local std::string t_thread_name = "UNKNOWN";

pid_t Thread::GetThreadId()
{
	return syscall(SYS_gettid);
}

Thread* Thread::GetThis()
{
    return t_thread;
}

const std::string& Thread::GetName() 
{
    return t_thread_name;
}

void Thread::SetName(const std::string &name) 
{
    if (t_thread) 
    {
        t_thread->name_ = name;
    }
    t_thread_name = name;
}

Thread::Thread(std::function<void()> cb, const std::string &name): cb_(cb), name_(name) 
{
    int rt = pthread_create(&thread_, nullptr, &Thread::run, this);
    if (rt) 
    {
        std::cerr << "pthread_create thread fail, rt=" << rt << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    // 等待线程函数完成初始化
    semaphore_.wait();
}

Thread::~Thread() 
{
    if (thread_) 
    {
        pthread_detach(thread_);
        thread_ = 0;
    }
}

void Thread::join() 
{
    if (thread_) 
    {
        int rt = pthread_join(thread_, nullptr);
        if (rt) 
        {
            std::cerr << "pthread_join failed, rt = " << rt << ", name = " << name_ << std::endl;
            throw std::logic_error("pthread_join error");
        }
        thread_ = 0;
    }
}

void* Thread::run(void* arg) 
{
    Thread* thread = (Thread*)arg;

    t_thread       = thread;
    t_thread_name  = thread->name_;
    thread->id_   = GetThreadId();
    pthread_setname_np(pthread_self(), thread->name_.substr(0, 15).c_str());

    std::function<void()> cb;
    cb.swap(thread->cb_); // swap -> 可以减少cb_中智能指针的引用计数
    
    // 初始化完成
    thread->semaphore_.signal();

    cb();
    return 0;
}

}  // namespace sylar