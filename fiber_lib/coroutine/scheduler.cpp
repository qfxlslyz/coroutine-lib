#include "scheduler.h"

static bool debug = false;

namespace sylar {

static thread_local Scheduler* t_scheduler = nullptr;

Scheduler* Scheduler::GetThis()
{
	return t_scheduler;
}

void Scheduler::SetThis()
{
	t_scheduler = this;
}

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name):
useCaller_(use_caller), name_(name)
{
	assert(threads>0 && Scheduler::GetThis()==nullptr);

	SetThis();

	Thread::SetName(name_);

	// 使用主线程当作工作线程
	if(use_caller)
	{
		threads --;

		// 创建主协程
		Fiber::GetThis();

		// 创建调度协程
		schedulerFiber_.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false)); // false -> 该调度协程退出后将返回主协程
		Fiber::SetSchedulerFiber(schedulerFiber_.get());
		
		rootThread_ = Thread::GetThreadId();
		threadIds_.push_back(rootThread_);
	}

	threadCount_ = threads;
	if(debug) std::cout << "Scheduler::Scheduler() success\n";
}

Scheduler::~Scheduler()
{
	assert(stopping()==true);
	if (GetThis() == this) 
	{
        t_scheduler = nullptr;
    }
    if(debug) std::cout << "Scheduler::~Scheduler() success\n";
}

void Scheduler::start()
{
	std::lock_guard<std::mutex> lock(mtx_);
	if(stopping_)
	{
		std::cerr << "Scheduler is stopped" << std::endl;
		return;
	}

	assert(threads_.empty());
	threads_.resize(threadCount_);
	for(size_t i=0;i<threadCount_;i++)
	{
		threads_[i].reset(new Thread(std::bind(&Scheduler::run, this), name_ + "_" + std::to_string(i)));
		threadIds_.push_back(threads_[i]->getId());
	}
	if(debug) std::cout << "Scheduler::start() success\n";
}

void Scheduler::run()
{
	int thread_id = Thread::GetThreadId();
	if(debug) std::cout << "Schedule::run() starts in thread: " << thread_id << std::endl;
	
	set_hook_enable(true);

	SetThis();

	// 运行在新创建的线程 -> 需要创建主协程
	if(thread_id != rootThread_)
	{
		Fiber::GetThis();
	}

	std::shared_ptr<Fiber> idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this));
	ScheduleTask task;
	
	while(true)
	{
		task.reset();
		bool tickle_me = false;

		{
			std::lock_guard<std::mutex> lock(mtx_);
			auto it = tasks_.begin();
			// 1 遍历任务队列
			while(it!=tasks_.end())
			{
				if(it->thread!=-1&&it->thread!=thread_id)
				{
					it++;
					tickle_me = true;
					continue;
				}

				// 2 取出任务
				assert(it->fiber||it->cb);
				task = *it;
				tasks_.erase(it); 
				activeThreadCount_++;
				break;
			}	
			tickle_me = tickle_me || (it != tasks_.end());
		}

		if(tickle_me)
		{
			tickle();
		}

		// 3 执行任务
		if(task.fiber)
		{
			{					
				std::lock_guard<std::mutex> lock(task.fiber->mtx_);
				if(task.fiber->getState()!=Fiber::TERM)
				{
					task.fiber->resume();	
				}
			}
			activeThreadCount_--;
			task.reset();
		}
		else if(task.cb)
		{
			std::shared_ptr<Fiber> cb_fiber = std::make_shared<Fiber>(task.cb);
			{
				std::lock_guard<std::mutex> lock(cb_fiber->mtx_);
				cb_fiber->resume();			
			}
			activeThreadCount_--;
			task.reset();	
		}
		// 4 无任务 -> 执行空闲协程
		else
		{		
			// 系统关闭 -> idle协程将从死循环跳出并结束 -> 此时的idle协程状态为TERM -> 再次进入将跳出循环并退出run()
            if (idle_fiber->getState() == Fiber::TERM) 
            {
            	if(debug) std::cout << "Schedule::run() ends in thread: " << thread_id << std::endl;
                break;
            }
			idleThreadCount_++;
			idle_fiber->resume();				
			idleThreadCount_--;
		}
	}
	
}

void Scheduler::stop()
{
	if(debug) std::cout << "Schedule::stop() starts in thread: " << Thread::GetThreadId() << std::endl;
	
	if(stopping())
	{
		return;
	}

	stopping_ = true;	

    if (useCaller_) 
    {
        assert(GetThis() == this);
    } 
    else 
    {
        assert(GetThis() != this);
    }
	
	for (size_t i = 0; i < threadCount_; i++) 
	{
		tickle();
	}

	if (schedulerFiber_) 
	{
		tickle();
	}

	if(schedulerFiber_)
	{
		schedulerFiber_->resume();
		if(debug) std::cout << "schedulerFiber_ ends in thread:" << Thread::GetThreadId() << std::endl;
	}

	std::vector<std::shared_ptr<Thread>> thrs;
	{
		std::lock_guard<std::mutex> lock(mtx_);
		thrs.swap(threads_);
	}

	for(auto &i : thrs)
	{
		i->join();
	}
	if(debug) std::cout << "Schedule::stop() ends in thread:" << Thread::GetThreadId() << std::endl;
}

void Scheduler::tickle()
{
}

void Scheduler::idle()
{
	while(!stopping())
	{
		if(debug) std::cout << "Scheduler::idle(), sleeping in thread: " << Thread::GetThreadId() << std::endl;	
		sleep(1);	
		Fiber::GetThis()->yield();
	}
}

bool Scheduler::stopping() 
{
    std::lock_guard<std::mutex> lock(mtx_);
    return stopping_ && tasks_.empty() && activeThreadCount_ == 0;
}


}