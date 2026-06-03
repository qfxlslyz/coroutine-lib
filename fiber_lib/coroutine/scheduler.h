#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "hook.h"
#include "fiber.h"
#include "thread.h"

#include <mutex>
#include <vector>

namespace sylar {

class Scheduler
{
public:
	Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name="Scheduler");
	
	// 基类析构函数定义为虚函数，确保基类指针删除派生类对象时能正确调用派生类的析构函数，然后再调用基类的析构函数
	// 只要一个类可能作为基类被继承，它的析构函数就需要声明为虚函数，这是C++的一条基本原则
	virtual ~Scheduler();
	
	const std::string& getName() const {return name_;}

public:	
	// 获取正在运行的调度器
	static Scheduler* GetThis();

protected:
	// 设置正在运行的调度器
	void SetThis();
	
public:	
	// 添加任务到任务队列
    template <class FiberOrCb>
    void scheduleLock(FiberOrCb fc, int thread = -1) 
    {
    	bool need_tickle;
    	{
    		std::lock_guard<std::mutex> lock(mtx_);
    		// empty ->  all thread is idle -> need to be waken up
    		need_tickle = tasks_.empty();
	        
	        ScheduleTask task(fc, thread);
	        if (task.fiber || task.cb) 
	        {
	            tasks_.push_back(task);
	        }
    	}
    	
    	if(need_tickle)
    	{
    		tickle();
    	}
    }
	
	// 启动线程池
	virtual void start();
	// 关闭线程池
	virtual void stop();	
	
protected:
	virtual void tickle();
	
	// 线程函数
	virtual void run();

	// 空闲协程函数
	virtual void idle();
	
	// 是否可以关闭
	virtual bool stopping();

	bool hasIdleThreads() {return idleThreadCount_>0;}

private:
	// 任务
	struct ScheduleTask
	{
		std::shared_ptr<Fiber> fiber;
		std::function<void()> cb;
		int thread; // 指定任务需要运行的线程id

		ScheduleTask()
		{
			fiber = nullptr;
			cb = nullptr;
			thread = -1;
		}

		ScheduleTask(std::shared_ptr<Fiber> f, int thr)
		{
			fiber = f;
			thread = thr;
		}

		ScheduleTask(std::shared_ptr<Fiber>* f, int thr)
		{
			fiber.swap(*f);
			thread = thr;
		}	

		ScheduleTask(std::function<void()> f, int thr)
		{
			cb = f;
			thread = thr;
		}		

		ScheduleTask(std::function<void()>* f, int thr)
		{
			cb.swap(*f);
			thread = thr;
		}

		void reset()
		{
			fiber = nullptr;
			cb = nullptr;
			thread = -1;
		}	
	};

private:
	std::string name_;
	// 互斥锁 -> 保护任务队列
	std::mutex mtx_;
	// 线程池
	std::vector<std::shared_ptr<Thread>> threads_;
	// 任务队列
	std::vector<ScheduleTask> tasks_;
	// 存储工作线程的线程id
	std::vector<int> threadIds_;
	// 需要额外创建的线程数
	size_t threadCount_ = 0;
	// 活跃线程数
	std::atomic<size_t> activeThreadCount_ = {0};
	// 空闲线程数
	std::atomic<size_t> idleThreadCount_ = {0};

	// 主线程是否用作工作线程
	bool useCaller_;
	// 如果是 -> 需要额外创建调度协程
	std::shared_ptr<Fiber> schedulerFiber_;
	// 如果是 -> 记录主线程的线程id
	int rootThread_ = -1;
	// 是否正在关闭
	bool stopping_ = false;	
};

}  // namespace sylar

#endif