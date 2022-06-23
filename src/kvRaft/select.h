#ifndef _SELECT_H
#define _SELECT_H

#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "locker.h"
using namespace std;

class Select{
public:
    Select(string fifoName);
    int timeout;
    string fifoName;
    char op;
    locker m_lock;
    cond m_cond;
    void mySelect();
    static void* wait_time(void* arg);
    static void* work(void* arg);
    static void* setTimeOut(void* arg);
    static void* send(void* arg);
    static void* test(void* arg);
};

Select::Select(string fifoName){
    this->fifoName = fifoName;
    pthread_t tid;
    int ret = mkfifo(fifoName.c_str(), 0664);
    pthread_create(&tid, NULL, send, this);
    pthread_detach(tid);
    pthread_t test_tid;
    pthread_create(&test_tid, NULL, test, this);
    pthread_detach(test_tid);
}

void* Select::send(void* arg){
    Select* s = (Select*)arg;
    int fd = open(s->fifoName.c_str(), O_WRONLY);
    char* buf = "12345";
    sleep(1);
    write(fd, buf, strlen(buf) + 1);
}

void* Select::wait_time(void* arg){
    sleep(2);
}

void* Select::setTimeOut(void* arg){
    Select* select = (Select*)arg;
    pthread_t tid;
    void* ret;
    pthread_create(&tid, NULL, wait_time, NULL);
    pthread_join(tid, &ret);
    select->m_lock.lock();
    select->op = '1';
    select->m_cond.signal();
    select->m_lock.unlock();
}

void* Select::work(void* arg){
    Select* select = (Select*)arg;
    char buf[100];
    int fd = open(select->fifoName.c_str(), O_RDONLY);
    read(fd, buf, sizeof(buf));
    select->m_lock.lock();
    select->op = '2';
    select->m_cond.signal();
    select->m_lock.unlock();
}

void Select::mySelect(){
    pthread_t wait_tid;
    pthread_create(&wait_tid, NULL, setTimeOut, this);
    pthread_detach(wait_tid);
    pthread_t work_tid;
    pthread_create(&work_tid, NULL, work, this);
    pthread_detach(work_tid);
    m_lock.lock();
    m_cond.wait(m_lock.getLock());
    switch(op){
        case '1':
            printf("time is out\n");
            break;
        case '2':
            printf("recv data\n");
            break;
    }
    m_lock.unlock();
}

void* Select::test(void* arg){
    Select* s = (Select*)arg;
    s->mySelect();
}

#endif