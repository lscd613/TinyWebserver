#ifndef THREADPOOL_H
#define THREADPOOL_H
#include "../Locker/Locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include <cstdio>
#include <pthread.h>
#include <list>
#include <exception>

template <typename T>
class Threadpool
{
public:
    Threadpool(connection_pool *connPool,int m_thread_number=8, int max_request = 10000);
    ~Threadpool();
    bool append(T* request);

private:
    static void* worker(void*);
    void run();

private:
    int m_thread_number;//最大线程数
    int m_max_requests;//请求队列的最大请求数
    connection_pool* m_connPool;//数据库
    pthread_t *m_threads;//线程数组
    std::list<T*> m_workqueue;//请求队列
    Locker m_queueLocker;//请求队列的互斥锁
    Sem m_queuestat;//是否有任务需要处理
    bool m_stop; //是否结束线程
};

template <typename T>
Threadpool<T>::Threadpool(connection_pool *connPool,
int thread_number, int max_requests):
m_connPool(connPool),m_thread_number(thread_number),
m_max_requests(max_requests),m_stop(false)
{
    //用户输入的异常处理
    if(thread_number<=0||max_requests<=0){
        throw std::exception();
    }

    m_threads= new pthread_t[m_thread_number];
    if(m_threads==NULL) throw std::exception();

    //线程创建和分离
    for(int i=0;i<thread_number;i++){
        printf("%d\n",i+1);
        if(pthread_create(m_threads+i,NULL,worker,this)!=0){
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])!=0){
            delete[] m_threads;
            throw std::exception();
        }
    }  
}

template <typename T>
Threadpool<T>::~Threadpool()
{
    delete []m_threads;
    m_stop = true;
}


template <typename T>
bool Threadpool<T>::append(T* request)
{
    m_queueLocker.lock();
    if(m_workqueue.size()>=m_max_requests){
        m_queueLocker.unlock();//return之前一定要记得解锁
        return false;
    }
    m_workqueue.push_back(request);
    m_queueLocker.unlock();
    m_queuestat.post();//信号量要维护
    return true;
}

template <typename T>
void* Threadpool<T>::worker(void* arg)
{
    Threadpool *pool=(Threadpool*)arg;
    pool->run();
    return pool;
}


template <typename T>
void Threadpool<T>::run()
{
    while(!m_stop){
        //wait
        m_queuestat.wait();
        //上锁看请求队列
        m_queueLocker.lock();
        if(m_workqueue.empty()){
            m_queueLocker.unlock();
            continue;
        }
        T *request=m_workqueue.front();
        m_workqueue.pop_front();
        m_queueLocker.unlock();
        if(!request) continue;
        
        connectionRAII mysqlcon(&request->mysql,m_connPool);
        request->process();

    }
}






#endif