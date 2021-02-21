# WebServer

## 已完成
### 线程同步机制封装类 `lock`     
> 多线程同步，确保任意时刻只有一个线程能进入关键代码段
* 信号量 `sem`
    * `sem.wait()`
    * `sem.post()`
    * `sem.getSV()`
* 互斥锁 `locker`
    * `locker.lock()`
    * `locker.unlock()`
    * `locker.get()`
* 条件变量 `cond`
    * `cond.wait()`
    * `cond.timewait()`
    * `cond.signal()`
    * `cond.broadcast()`

### 半同步/半反应堆线程池 `threadpool`
> 线程池的设计模式为半同步/半反应堆，其中反应堆具体为Proactor事件处理模式。
> 主线程为异步线程，负责监听lfd，接受socker新连接，当lfd发生读写事件后，将任务插入到请求队列中。工作线程从请求队列中取出任务，完成读写数据的处理。

```shell
pthreadpool() -> pthread_create() -> worker() -> run()
                                                   ^
                                      request      | request
                             append() -------> workqueque
```

* `pthreadpool`
    * 线程池 `pthreads`
    * 请求队列 `workqueue`
    * `pthreadpool()` 线程池初始化      
        创建线程时将类的对象作为参数传递给静态处理函数，在静态处理函数中调用动态方法`run()`
        * 根据线程池中当前线程数量循环创建线程并且设置线程分离，初始化其它成员变量。
        * 创建线程 -> 线程运行处理函数`worker`，传入的是`this`指针
    * `worker()` 线程处理函数    
        内部调用`run()`，完成线程处理要求。
        * 静态成员函数不能调用类的非静态成员，不含`this`指针，所以传入了`this`指针
        * 将传入的`this`指针参数强转为线程池类。
        * 调用`run()`
    * `append()` 向请求队列中添加任务  
        通过`locker`保证线程安全，完成后通过`sem`提醒线程有任务要处理。
        * `m_sem.post()`
    * `run()` 执行任务      
        工作线程从任务队列中取出某个任务进行处理
        * 从任务队列中取出一个任务 `request`
        * `request(http类)`调用`process()`进行处理

### http连接请求处理类 `http_coon`
> http报文处理流程:    
    浏览器发出http连接请求 -> 主线程创建http对象接受请求并把所有数据存入buffer中 -> 将该http对象插入任务队列 -> 工作线程从任务队列中取出一个http对象进行处理 -> 调用process_read函数，通过主、从状态机对请求报文进行解析 -> 跳转do_request函数生产相应报文，通过process_write写入buffer -> 返回给浏览器


## 未完成
* 定时器处理非活动连接
* 同步/异步日志系统
* 数据库连接池
* 同步线程注册和登录校验
* 压力测试