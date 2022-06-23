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

void myWrite(int fd, vector<string>& str){
    int len = 0;
    char buf[2];
    sprintf(buf,"\n");
    for(auto s : str){
        printf("%s\n", s.c_str());
        len = write(fd, s.c_str(), s.size());
        write(fd, buf, strlen(buf));
        if(len == -1){
            perror("write");
            exit(-1);
        }
    }
}

int main(){
    int fd = open("a.txt", O_WRONLY | O_CREAT | O_APPEND, 0664);
    vector<string> str;
    string a = "qbu 1";
    string b = "sas 11";
    string c = "abc 111";
    str.push_back(a);
    str.push_back(b);
    str.push_back(c);
    myWrite(fd, str);
    close(fd);
    return 0;
}