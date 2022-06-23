#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <bits/stdc++.h>
using namespace std;

#define REDUCE_TASK_NUM 8

class KeyValue{
public:
    string Key;
    string value;
};

int ihash(string str){
    int sum = 0;
    for(int i = 0; i < str.size(); i++){
        sum += (str[i] - '0');
    }
    return sum % REDUCE_TASK_NUM;
}

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

KeyValue splitText(char* file){
    int fd = open(file, O_RDONLY);
    int length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char buf[length];
    bzero(buf, length);
    int len = read(fd, buf, length);
    KeyValue kv;
    kv.Key = string(file);
    kv.value = string(buf);
    close(fd);
    return kv;
}

vector<KeyValue> mapF(KeyValue kv){
    vector<KeyValue> kvs;
    int len = kv.value.size();
    char content[len + 1];
    strcpy(content, kv.value.c_str());
    vector<string> str = split(content);
    for(const auto& s : str){
        KeyValue tmp;
        tmp.Key = s;
        tmp.value = "1";
        kvs.emplace_back(tmp);
    }
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

int main(int argc, char* argv[]){
    if(argc < 2){
        printf("error , the format is ./main ...\n");
        exit(-1);
    }
    for(int i = 1; i < argc; i++){
        KeyValue kv = splitText(argv[i]);
        vector<KeyValue> kvs = mapF(kv);
        string path;
        path = "out-x-" + to_string(i) + ".txt";
        int ret = access(path.c_str(), F_OK);
        if(ret == 0) remove(path.c_str());
        writeInDisk(kvs, path);
    }

}