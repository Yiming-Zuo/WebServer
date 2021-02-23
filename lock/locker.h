#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 信号量
class sem {
public:
    sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(int num) {
        if (sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy(&m_sem);
    }
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    bool post() {
        return sem_post(&m_sem) == 0;
    }
    int getSV() {
        int sval;
        sem_getvalue(&m_sem, &sval);
        return sval;
    }
private:
    sem_t m_sem;
};

// 互斥锁
class locker {
public:
    locker() {
        if (pthread_mutex_init(&m_mtx, NULL) != 0) {
            throw std::exception();
        }
    }
    ~locker() {
        pthread_mutex_destroy(&m_mtx);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mtx) == 0;
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mtx) == 0;
    }
    pthread_mutex_t *get() {
        return &m_mtx;
    }
private:
    pthread_mutex_t m_mtx;
};

// 条件变量
class cond {
public:
    cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
        // if (pthread_cond_destroy(&m_cond) != 0) {
        //     throw std::exception();
        // }
    }
    bool wait(pthread_mutex_t *mtx) {
        return pthread_cond_wait(&m_cond, mtx) == 0;
    }
    // 指定时间后解除阻塞
    bool timewait(pthread_mutex_t *mtx, struct timespec t) {
        return pthread_cond_timedwait(&m_cond, mtx, &t);
    }
    // 唤醒一个线程
    // 一个生产者，一次生产一个产品
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    // 唤醒所有线程
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

#endif // !LOCKER_H