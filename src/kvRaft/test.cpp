#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <bits/stdc++.h>
#include <unistd.h>
#include "locker.h"
using namespace std;

int num = 0;

class ApplyMsg {
public:
    bool commandValid;
	string command;
    int commandIndex;
    int commandTerm;
};

class Raft{
public:
    static void* applyLogLoop(void* arg);
    Raft(){
        // m_msgs.clear();
        send.init(0);
        recv.init(0);
        pthread_t tid;
        pthread_create(&tid, NULL, applyLogLoop, this);
        pthread_detach(tid);
    }
    locker m_lock;
    sem send;
    sem recv;
    vector<ApplyMsg> m_msgs;
};
void* Raft::applyLogLoop(void* arg){
    Raft* raft = (Raft*)arg;
    usleep(10000);
    vector<ApplyMsg> msgs;
    raft->m_lock.lock();
    for(int i = 0; i < 25; i++){
        ApplyMsg msg;
        msg.command = to_string(i);
        msg.commandIndex = i;
        msg.commandValid = true;
        msg.commandTerm = 1;
        msgs.push_back(msg);
    }
    raft->m_lock.unlock();
    for(int i = 0; i < msgs.size(); i++){
        raft->recv.wait();
        raft->m_msgs.push_back(msgs[i]);
        raft->send.post();
    }
}

class kv{
public:
    kv();
    static void* applyLoop(void* arg);
    Raft raft;
    locker m_lock;
    const char* fifoName;
};

void* kv::applyLoop(void* arg){
    kv* k = (kv*)arg;
    k->raft.recv.init(1);
    while(1){
        k->raft.send.wait();
        printf(" num is %d\n", k->raft.m_msgs.back().commandIndex);
        int fd = open(k->fifoName, O_WRONLY);
        const char* buf = to_string(num).c_str();
        num++;
        // usleep(1000);    担心的是put完告诉对应的fifo已完成请求时，两次回复通过管道的通知先后写入一起
        //只发送了一次，可以写一次关一次(一般推荐),可以sleep(不建议),也可以再度封装fifo
        //或者每条命令有一个新管道，go的实现参考就是这么做的(很推荐)

        // type OpContext struct {
        //     op *Op
        //     committed chan byte     -> 每个Op上下文都有一个对应的管道
        
        //     wrongLeader bool 	// 因为index位置log的term不一致, 说明leader换过了
        //     ignored bool // 因为req id过期, 导致该日志被跳过
        
        //     // Get操作的结果
        //     keyExist bool
        //     value string
        // }

        write(fd, buf, strlen(buf) + 1);

        k->raft.recv.post();
    }
}

kv::kv(){
    Raft raft;
    pthread_t tid;
    fifoName = "myFifo";
    int ret = mkfifo(fifoName, 0664);
    pthread_create(&tid, NULL, applyLoop, this);
    pthread_detach(tid);
}

class myTest{
public:
    myTest(int num){this->a = 1;}
    int a;
};

int main(){
    // kv k;
    // while(1){
    //     char buf[10];
    //     int fd = open(k.fifoName, O_RDONLY);
    //     int len = read(fd, buf, sizeof(buf));
    //     printf("recv buf is %s\n", buf);
    // }
    unordered_map<int, myTest*> testMap;
    myTest test1(3);
    myTest test2(3);
    testMap[1] = &test1;
    testMap[2] = &test2;
    printf("before erase test1 is %d\n", testMap[1]->a);
    testMap.erase(1);
    // printf("after erase test1 is %d\n", testMap[1]->a);
    printf("oringal test1 is %d\n", test1.a);
}
