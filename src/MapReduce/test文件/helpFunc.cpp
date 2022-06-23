#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <vector>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <bits/stdc++.h>
using namespace std;

#define MAP_TASK_NUM 8
#define REDUCE_TASK_NUM 8

class KeyValue{
public:
    string key;
    string value;
};

void removeFiles(){
    string path;
    for(int i = 0; i < MAP_TASK_NUM; i++){
        for(int j = 0; j < REDUCE_TASK_NUM; j++){
            path = "mr-" + to_string(i) + "-" + to_string(j);
            cout<<path<<endl;
            int ret = access(path.c_str(), F_OK);
            if(ret == 0) remove(path.c_str());
        }
    }

}

KeyValue getContent(char* file){
    int fd = open(file, O_RDONLY);
    int length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char buf[length];
    bzero(buf, length);
    int len = read(fd, buf, length);
    if(len != length){
        perror("read");
        exit(-1);
    }
    KeyValue kv;
    kv.key = string(file);
    kv.value = string(buf);
    close(fd);
    return kv;
}

vector<string> split(string text, char op){
    int n = text.size();
    vector<string> str;
    string tmp = "";
    for(int i = 0; i < n; i++){
        if(text[i] != op){
            tmp += text[i];
        }else{
            if(tmp.size() != 0) str.push_back(tmp);
            tmp = "";
        }
    }
    return str;
}

string split(string text){
    string tmp = "";
    for(int i = 0; i < text.size(); i++){
        if(text[i] != ','){
            tmp += text[i];
        }else break;
    }
    return tmp;
}

vector<string> Myshuffle(int reduceTaskNum){
    string path;
    //for(int i = 0; i < REDUCE_TASK_NUM; i++){
    //path = "mr-out-" + to_string(i);
    //int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0664);
    vector<string> str;
    unordered_map<string, string> hash;
    for(int j = 0; j < MAP_TASK_NUM; j++){
        path = "mr-" + to_string(j) + "-" + to_string(reduceTaskNum);
        char text[path.size() + 1];
        strcpy(text, path.c_str());
        KeyValue kv = getContent(text);
        string tmp = kv.value;
        int len = kv.value.size();
        char content[len + 1];
        strcpy(content, kv.value.c_str());
        vector<string> retStr = split(content, ' ');
        str.insert(str.end(), retStr.begin(), retStr.end());
    }
    for(const auto& a : str){
        hash[split(a)] += "1";
    }
    vector<string> retStr;
    for(const auto& a : hash){
        retStr.push_back(a.first + " " + a.second);
    }
    return retStr;
    //}
}

vector<string> getAllfile(string path, int op){
    DIR *dir = opendir(path.c_str());
    vector<string> ret;
    if (dir == NULL)
    {
        printf("[ERROR] %s is not a directory or not exist!", path.c_str());
        return ret;
    }
    struct dirent* entry;
    while ((entry=readdir(dir)) != NULL)
    {
        int len = strlen(entry->d_name);
        int oplen = to_string(op).size();
        if(len - oplen != 5) continue;
        string filename(entry->d_name);
        string cmp_str = filename.substr(len - oplen, oplen);
        if(cmp_str == to_string(op)){
            ret.push_back(entry->d_name);
        }
    }
    closedir(dir);
    return ret;
}

int main(){
    //removeFiles();
    vector<string> str = getAllfile(".", 0);
    for(auto a : str){
        printf("file is %s\n", a.c_str());
    }
    return 0;
}
