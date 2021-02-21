#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <cstdio>
#include <list>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"

template <typename T>
class threadpool {
public:
    threadpool(int pthread_num = 10, int max_requests = 10000);
    ~threadpool();
    // 像请求队列中插入请求
    bool append(T *request);

// 线程处理函数和运行函数要设为私有属性
private:
    // 线程处理函数：不断的从工作队列中取出请求任务并且处理
    // 需要设置为静态成员函数：因为pthread_create需要出入的处理函数指针类型要为void *，普通成员函数会默认传入this指针，不能和线程函数参数匹配
    static void *worker(void *arg);
    // 运行函数
    void run();

private:
    int m_pthread_number;  // 当前线程数量
    pthread_t *m_pthreads;  // 线程池数组
    std::list<T *> m_workqueue;  // 请求队列
    int m_max_requests;  // 请求队列最大容量
    locker m_locker;  // 保护请求队列的互斥锁
    sem m_sem;  // 信号量 判断是否有任务需要处理
    bool m_stop;  // 是否结束线程
};

// 构造函数：线程池创建与回收
template <typename T>
threadpool<T>::threadpool(int pthread_num, int max_requests) :
    m_pthread_number(pthread_num), m_max_requests(max_requests), m_stop(false) 
{
    if (pthread_num <= 0 || max_requests <= 0) {
        throw std::exception();
    }
    // 线程池初始化
    m_pthreads = new pthread_t[m_pthread_number];
    if (!m_pthreads) {
        throw std::exception();
    }
    // 循环创建线程，并设置线程分离：操作系统自动回收线程资源
    for (int i = 0; i < m_pthread_number; i++) {
        // 创建线程并且运行处理函数
        if (pthread_create(m_pthreads + i, NULL, worker, this) != 0) {
            delete[] m_pthreads;  // delete[] 对于类对象会先调用对象的析构函数
            throw std::exception();
        }
        if (pthread_detach(m_pthreads[i])) {
            delete[] m_pthreads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_pthreads;
}

// 向请求数列中添加任务
// 注意线程同步
template<typename T>
bool threadpool<T>::append(T *requeset) {
    m_locker.lock();  // 线程安全
    if (m_workqueue.size() > m_max_requests) {
        m_locker.unlock();
        return false;
    }
    m_workqueue.push_back(requeset);
    m_locker.unlock();

    // sem提醒线程有任务要处理
    m_sem.post();
    return true;
}

// 线程处理函数
template<typename T>
void * threadpool<T>::worker(void *arg) {
    // 静态成员函数没有this指针，不能调用其它数据成员。
    // 将this指针强转为线程池类对象，调用成员方法
    threadpool *ptrPool = (threadpool *)arg;
    ptrPool->run();

    return ptrPool;
}

// TODO: m_action_model
// 线程从工作队列中循环取出一个请求进行处理
template<typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_sem.wait();  // 信号量等待
        m_locker.lock();
        if (m_workqueue.empty()) {
            m_locker.unlock();
            continue;  // 等待任务
        }

        // 取出请求
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_locker.unlock();
        if (!request) {
            continue;
        }

        // 处理请求
        request->process();

    }
}

#endif // !THREADPOOL_
