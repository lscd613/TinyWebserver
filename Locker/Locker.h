#ifndef LOCKER_H
#define LOCKER_H
#include <exception>
#include <semaphore.h>
#include <pthread.h>
//信号量
class Sem{
public:
    Sem(){
        if(sem_init(&m_sem,0,0)!=0){
            throw std::exception();
        }
    }
    Sem(int num){
        if(sem_init(&m_sem,0,num)!=0){
            throw std::exception();
        }
    }
    ~Sem(){
        sem_destroy(&m_sem);
    }

    bool post(){
        return sem_post(&m_sem) == 0;
    }

    bool wait(){
        return sem_wait(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

//条件变量
class Cond{
public:

private:

};

//Locker
class Locker{
public:

private:

};

#endif
