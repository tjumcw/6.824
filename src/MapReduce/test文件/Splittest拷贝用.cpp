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
#include <pthread.h>
#include <dlfcn.h>
using namespace std;



int main(int argc, char* argv[]){
    if(argc < 2){
        cout<<"lack parameter !"<<endl;
    }
    vector<string> str;
    for(int idx = 1; idx < argc; idx++){
        int fd = open(argv[idx] ,O_RDONLY);
        int length = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        char buf[length];
        bzero(buf, length);
        int len = read(fd, buf, length);
        if(len != length){
            perror("read");
            exit(-1);
        }
        string tmp = "";
        for(int i = 0; i < length; i++){
            if((buf[i] >= 'A' && buf[i] <= 'Z') || (buf[i] >= 'a' && buf[i] <= 'z')){
                tmp += buf[i];
            }else{
                if(tmp.size() != 0) str.push_back(tmp);      
                tmp = "";
            }
        }
        if(tmp.size() != 0) str.push_back(tmp);
    }
    unordered_map<string, int> hash;
    for(auto a : str){
        hash[a]++;
    }  
    for(auto a : hash){
        cout<<a.first<<" : "<<a.second<<endl;
    }
    return 0;
}