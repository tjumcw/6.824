#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <bits/stdc++.h>
using namespace std;

class KeyValue{
public:
    string key;
    string value;
};

vector<string> split(char* text, int length){
    vector<string> str;
    string tmp = "";
    for(int i = 0; i < length; i++){
        if((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z')){
            tmp += text[i];
        }else{
            if(tmp.size() != 0) str.push_back(tmp);      
            tmp = "";
        }
    }
    if(tmp.size() != 0) str.push_back(tmp);
    return str;
}

extern "C" vector<KeyValue> mapF(KeyValue kv){
    vector<KeyValue> kvs;
    int len = kv.value.size();
    char content[len + 1];
    strcpy(content, kv.value.c_str());
    vector<string> str = split(content, len);
    for(const auto& s : str){
        KeyValue tmp;
        tmp.key = s;
        tmp.value = "1";
        kvs.emplace_back(tmp);
    }
    return kvs;
}

extern "C" vector<string> reduceF(vector<KeyValue> kvs, int reduceTaskIdx){
    vector<string> str;
    string tmp;
    for(const auto& kv : kvs){
        str.push_back(to_string(kv.value.size()));
    }
    return str;
}

