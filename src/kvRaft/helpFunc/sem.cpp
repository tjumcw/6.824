#include <pthread.h>
#include <semaphore.h>
#include <bits/stdc++.h>
#include "locker.h"
using namespace std;

class test{
public:
    test(){
        send.init(0);
        recv.init(0);
    }
    sem send;
    sem recv;
};

vector<int> nums;
void* work(void* arg){
    test* t = (test*)arg;
    t->recv.init(1);
    for(int i = 0; i < 10; i++){
        t->recv.wait();
        nums.push_back(i);
        t->send.post();
    }
}

int main(){

    test t;
    pthread_t tid;
    pthread_create(&tid, NULL, work, &t);
    pthread_detach(tid);
    while(1){
        t.send.wait();
        printf(" num is %d\n", nums.back());
        t.recv.post();
    }
}