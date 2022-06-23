#include <iostream>
#include <bits/stdc++.h>
using namespace std;

vector<string> help(string str){
    vector<string> ret;
    for(int i = 0; i < 3; i++){
        string tmp = "";
        tmp = str + to_string(i);
        ret.push_back(tmp);
    }
    return ret;
}

vector<string> split(string text, char op){
    int n = text.size();
    vector<string> str;
    string tmp = "";
    for(int i = 0; i < n; i++){
        if(text[i] != op){
            tmp += text[i];
        }else if(text[i] == op){
            if(tmp.size() != 0) str.push_back(tmp);
            tmp = "";
        }
    }
    if(tmp.size() != 0) str.push_back(tmp);
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


int main(){
    // vector<string> str;
    // string a = "aaa";
    // string b = "bbb";
    // string c = "ccc";
    // vector<string> para;
    // para.push_back(a);
    // para.push_back(b);
    // para.push_back(c);
    // for(int i = 0; i < 3; i++){
    //     vector<string> ret = help(para[i]);
    //     str.insert(str.end(), ret.begin(), ret.end());
    // }
    // cout<<str.size()<<endl;
    // for(auto a : str){
    //     cout<<a<<endl;
    // }

    
    string tmp = "abc,1 qbu,1 sas,1 abc,1 huh,1 abc,1 sas,1 jiu,1";
    vector<string> v_str = split(tmp, ' ');
    unordered_map<string, string> hash;
    for(const auto& a : v_str){
        hash[split(a)] += "1";
    }
    vector<string> ans;
    for(const auto& a : hash){
        ans.push_back(a.first + " " + a.second);
    }
    for(const auto& a : ans){
        cout<<a<<endl;
    }
    return 0;
}