#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "locker.h"
#include "./buttonrpc-master/buttonrpc.hpp"
#include <bits/stdc++.h>
#include <pthread.h>
using namespace std;

#define MAP_TASK_NUM 10
#define REDUCE_TASK_NUM 8

//可能造成的bug，考虑write多次写，每次写1024用while读进buf
//c_str()返回的是一个临时指针，值传递没事，但若涉及地址出错

class KeyValue{
public:
    string Key;
    string value;
};

vector<string> split(char* text){
    vector<string> str;
    char* tmp = text;
    char* buf = strtok(tmp, ",.;:'?!()/\"[]()_-~*$@#\n ");
    while(buf != NULL){
        str.push_back(buf);
        buf = strtok(NULL, ",.;:'?!()/\"[]()_-~*$@#\n ");
    }
    return str;
}

void splitText(int fd, vector<KeyValue>& kv){
    int length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char buf[length];
    bzero(buf, length);
    int len = read(fd, buf, length);
    vector<string> str = split(buf);
    for(const auto& s : str){
        KeyValue tmp;
        tmp.Key = s;
        tmp.value = "1";
        kv.emplace_back(tmp);
    }
}

int ihash(string str){
    int sum = 0;
    for(int i = 0; i < str.size(); i++){
        sum += (str[i] - '0');
    }
    return sum % REDUCE_TASK_NUM;
}

void writeInDisk(const vector<KeyValue>& kv, string path){
    int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0664);
    string tmp;
    for(int i = 0; i < kv.size(); i++){
        tmp += (kv[i].Key + ":" + kv[i].value + " ");
    }
    cout<<strlen(tmp.c_str())<<endl;
    int len = write(fd, tmp.c_str(), tmp.size());
    if(len == -1){
        perror("write");
        exit(-1);
    }
    close(fd);
}

void* worker(void* arg){
    buttonrpc client;
    client.as_client("127.0.0.1", 5555);
    client.set_timeout(5000);
    string task = client.call<string>("assignTask").val();
    int fd = open(task.c_str(), O_RDONLY);
    vector<KeyValue> kv;
    splitText(fd, kv);
    close(fd);
    /*string path;
    path = "out-x-1.txt";
    int ret = access(path.c_str(), F_OK);
    if(ret == 0) remove(path.c_str());
    writeInDisk(kv, path);*/
} 

int main(){
    buttonrpc work_client;
    work_client.as_client("127.0.0.1", 5555);
    work_client.set_timeout(5000);
    pthread_t tid[MAP_TASK_NUM];
    for(int i = 0; i < MAP_TASK_NUM; i++){
        pthread_create(&tid[i], NULL, worker, NULL);
        pthread_detach(tid[i]);
    }
    while(1){
        if(work_client.call<bool>("isMapDone").val()){
            break;
        }
    }
}