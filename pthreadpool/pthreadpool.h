#ifndef PTHREADPOOL_H
#define PTHREADPOOL_H

#include <cstdio>
#include <list>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"

template <typename T>
class pthreadpool {
public:
    pthreadpool(connection_pool *connPool, int pthread_num = 10, int max_requests = 10000);
    ~pthreadpool();

    // 像请求队列中插入请求
    bool append(T *request);

// 线程处理函数和运行函数要设为私有属性
private:
    // 线程处理函数
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
    connection_poll *m_connPool;  // 数据库连接池
};

// 构造函数：线程池创建与回收
template <typename T>
pthreadpool<T>::pthreadpool(connection_pool *connPool, int pthread_num = 10, int max_requests = 10000) : m_pthread_number(pthread_num), m_max_requests(max_requests), m_stop(false), m_connPool(connPool) {
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
        if (pthread_create(m_pthreads + i, NULL, worker, this)) {
            delete[] m_pthreads;  // delete[] 对于类对象会先调用对象的析构函数
            throw std::exception();
        }
        if (pthread_detach(m_pthreads[i])) {
            delete[] m_pthreads;
            throw std::exception();
        }
    }

}

// 向请求数列中添加任务
// 注意线程同步
template<typename T>
bool pthreadpool<T>::append(T *requeset) {
    if (m_workqueue.size() > m_max_requests) {
        return false;
    }
    m_locker.lock();  // 线程安全
    m_workqueue.push_back(requeset);
    m_locker.unlock();

    // sem提醒有任务要处理
    m_sem.post();
    return true;
}

#endif // !PTHREADPOOL_
