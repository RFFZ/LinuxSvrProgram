#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "locker.h"


template <typename T>
class threadPool
{
public:
    threadPool(int thread_number = 8, int max_requests = 1000);
    ~threadPool();
    bool append(T* request);

private:
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;
    int m_max_requests;
    pthread_t *m_threads;
    std::list<T*> m_workqueue;
    locker m_queuelocker;
    sem m_queuestat;
    bool m_stop;
};

template <typename T>
threadPool<T>::threadPool(int thread_number, int max_requests):m_thread_number(thread_number),
m_max_requests(max_requests),m_stop(false),m_threads(nullptr)
{
    if(thread_number <= 0 || max_requests <= 0)
    {
        throw std::exception();
    }

    m_threads = new thread_t[m_thread_number];
    if(!m_threads)
    {
        throw std::exception();
    }

    for(int i = 0; i< m_thread_number; ++i)
    {
        printf("create the %dth thread\n",i);
        if(pthread_create(m_threads + i,NULL,worker,this) != 0)
        {
            delete [] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i]))
        {
            
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadPool<T>::~threadPool()
{
    delete [] m_threads;
    m_stop = true;
}

template <typename T>
bool threadPool<T>::append(T* request)
{
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void* threadPool<T>::worker(void* arg)
{
    threadPool* pool = (threadPool*) arg;
    pool->run();
    return pool;
}

template <typename T>
void threadPool<T>::run()
{
    while(!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request)
        {
            continue;
        }
        request->process();
    }
}


#endif