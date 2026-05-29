# 项目介绍
**编译标准：** C++17

**编译指令：**
```bash
cd hook
g++ *.cpp -std=c++17 -o main -ldl -lpthread
```

**来源：** 开源C++高性能分布式服务器框架[sylar](https://github.com/sylar-yin/sylar)中的协程子模块

**定性：** 非对称协程，子协程不能再创建新的协程，即协程不能嵌套调用，子协程只能与线程主协程进行切换，是一种比较简单的协程实现模型

非对称协程的执行过程：
```text
执行主函数
切换：主函数 -> 协程A
执行协程A
切换：协程A -> 主函数
执行主函数
切换：主函数 -> 协程B
执行协程B
切换：协程B -> 主函数
执行主函数
...
```

其他优秀的协程实现：腾讯libco


# 上下文结构体定义
```c
#include <ucontext.h>
// 这个结构体是平台相关的，因为不同平台的寄存器不一样
// 下面列出的是每个平台都至少会包含的四个成员
typedef struct ucontext_t 
{
    // 当前上下文结束后，下一个激活的上下文对象的指针，只在当前上下文是由makecontext创建时有效
    struct ucontext_t *uc_link;

    // 当前上下文中阻塞的信号集合
    sigset_t uc_sigmask;

    // 当前上下文使用的栈内存空间，只在当前上下文由makecontext创建时有效
    stack_t uc_stack;

    // 保存和恢复上下文执行环境的关键，保存PC指针寄存器与栈指针寄存器的值
    mcontext_t uc_mcontext;
} ucontext_t;
```

# 主要使用的api
```c
// 获取当前的上下文
int getcontext(ucontext_t *ucp);

// 修改由getcontext获取到的上下文指针ucp，将其与一个函数指针func绑定，支持指定func运行时绑定的参数
// 在调用makecontext之前，必须手动为ucp->uc_stack分配内存空间，这段内存空间将作为func函数运行时的栈空间
// 同时也可以指定ucp->uc_link，用于在func执行完成之后恢复至uc_link所指向的上下文
// 如果不指定ucp->uc_link，那么在func函数结束时必须调用setcontext或swapcontext以重新指定一个有效的上下文，否则程序就跑飞了
// makecontex执行完成后，ucp就和func函数绑定了，调用setcontext或swapcontext激活ucp时，func就会被执行
void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);

// 恢复ucp指向的上下文，这个函数不会返回，而是会跳转到ucp上下文对应的func执行，相当于变相调用了func
int setcontext(const ucontext_t *ucp);

// 恢复ucp指向的上下文，同时将当前的上下文存储到oucp中
// 和setcontext一样，swapcontext也不会返回，而是转去执行ucp绑定的func，相当于调用了func
// swapcontext是sylar非对称协程实现的关键，线程主协程和子协程用这个接口进行上下文切换
int swapcontext(ucontext_t *oucp, const ucontext_t *ucp);
```

# 简单示例程序
执行结果：每隔1s递增打印cnt
```c
#include <stdio.h>
#include <ucontext.h>
#include <unistd.h>

int main()
{
    ucontext_t ctx;
    int cnt = 0;
    getcontext(&ctx);  // 获取上下文
    printf("%d\n", ++cnt);
    sleep(1);  // 休眠1s
    setcontext(&ctx);  // 恢复ctx指向的上下文

    return 0;
}
```

# 核心模块简单介绍
- **thread** ：弥补协程的缺点，通过**多线程+多协程**的方式，更好地利用多核CPU资源。
- **fiber** ：负责协程的创建、暂停、恢复等核心逻辑，是真正运行任务的单元。
- **scheduler** ：如果没有这个模块，就需要用户手动控制协程的执行与切换，不够灵活也不够智能。
- **ioscheduler** ：IO调度器，是 `scheduler` 模块的IO增强版。协程库主要用于服务器项目，该类使用 `epoll` 监听 `fd` 上的读写事件，事件触发后将协程加入调度器等待执行。
- **timer** ：服务器通常需要定时器处理定时任务。该模块负责定时器的创建、添加与删除，内部使用**最小堆**管理超时时间；定时器触发时通过 `tickle` 信号唤醒 `ioscheduler` 中的 `epoll_wait`。
- **hook** ：只有结合 `hook` 与 `ioscheduler` ，才能真正实现非阻塞的服务器框架。虽然前面已经实现了协程调度，但如果不修改系统调用行为，就无法在阻塞时自动挂起/恢复协程。例如 `sleep(1)` 仍然会阻塞整个线程，无法体现协程优势。因此通过 `hook`替换原始系统调用，增强其逻辑：将 `sleep(1)` 这类操作转为定时任务，放入 `timer` 的最小堆中等待超时，之后唤醒 `epoll` ，再将协程交回调度器执行。 