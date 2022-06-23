#ifndef __COMMON__H
#define __COMMON__H

#include <bits/stdc++.h>
#include "./buttonrpc-master/buttonrpc.hpp"
using namespace std;
#define NShards 10

class Config{
public:
    Config(){
        configNum = 0;
        shards.resize(NShards, 0);
        groups.clear();
    }
    int configNum;
    vector<int> shards;
    unordered_map<int, vector<string>> groups;
};

class JoinArgs{
public:
    void getServersShardInfoFromMap(unordered_map<int, vector<string>> servers);
    string serversShardInfo;
    int clientId;
    int requestId;

    friend Serializer& operator >> (Serializer& in, JoinArgs& d) {
		in >> d.serversShardInfo >> d.clientId >> d.requestId;
		return in;
	}
	friend Serializer& operator << (Serializer& out, JoinArgs d) {
		out << d.serversShardInfo << d.clientId << d.requestId;
		return out;
	}
};

void JoinArgs::getServersShardInfoFromMap(unordered_map<int, vector<string>> servers){
    serversShardInfo = "";
    for(const auto& ser : servers){
        serversShardInfo += to_string(ser.first) + ":";
        for(const auto& v : ser.second){
            serversShardInfo += v + ":";
        }
        serversShardInfo += "|";
    }
}

unordered_map<int, vector<string>> getMapFromServersShardInfo(string serversShardInfo){
    unordered_map<int, vector<string>> myMap;
    vector<string> str;
    string tmp = "";
    for(int i = 0; i < serversShardInfo.size(); i++){
        if(serversShardInfo[i] != '|'){
            tmp += serversShardInfo[i];
        }else{
            if(tmp.size() != 0){
                str.push_back(tmp);
                tmp = "";
            }
        }
    }
    tmp = "";
    for(int i = 0; i < str.size(); i++){
        int num = 0;
        vector<string> info;
        for(int j = 0; j < str[i].size(); j++){
            if(str[i][j] != ':'){
                tmp += str[i][j];
            }else{
                if(tmp.size() != 0){
                    info.push_back(tmp);
                    tmp = "";
                }
            }
        }
        num = stoi(info[0]);
        info.erase(info.begin());
        myMap[num] = info; 
    }
    return myMap;
}

class JoinReply{
public:
    bool isWrongLeader;
};

class LeaveArgs{
public:
    void getGroupIdsInfoFromVector(vector<int>& gIds);
    string groupIdsInfo;
    int clientId;
    int requestId;

    friend Serializer& operator >> (Serializer& in, LeaveArgs& d) {
		in >> d.groupIdsInfo >> d.clientId >> d.requestId;
		return in;
	}
	friend Serializer& operator << (Serializer& out, LeaveArgs d) {
		out << d.groupIdsInfo << d.clientId << d.requestId;
		return out;
	}
};

void LeaveArgs::getGroupIdsInfoFromVector(vector<int>& gIds){
    groupIdsInfo = "";
    for(const auto& id : gIds){
        groupIdsInfo += to_string(id) + ":";
    }
}

vector<int> GetVectorOfIntFromString(string groupIdsInfo){
    vector<int> gIds;
    string tmp = "";
    for(int i = 0; i < groupIdsInfo.size(); i++){
        if(groupIdsInfo[i] != ':'){
            tmp += groupIdsInfo[i];
        }else{
            if(tmp.size() != 0){
                gIds.push_back(stoi(tmp));
                tmp = "";
            }
        }
    }
    return gIds;
}

class LeaveReply{
public:
    bool isWrongLeader;
};

class MoveArgs{
public:
    void getStringOfShardAndGroupId(int shard, int gId);
    string shardAndGroupIdInfo;
    int clientId;
    int requestId;

    friend Serializer& operator >> (Serializer& in, MoveArgs& d) {
		in >> d.shardAndGroupIdInfo >> d.clientId >> d.requestId;
		return in;
	}
	friend Serializer& operator << (Serializer& out, MoveArgs d) {
		out << d.shardAndGroupIdInfo << d.clientId << d.requestId;
		return out;
	}
};

void MoveArgs::getStringOfShardAndGroupId(int shard, int gId){
    shardAndGroupIdInfo += to_string(shard) + ":" + to_string(gId) + ":";
}

vector<int> getShardAndGroupId(string shardAndGroupIdInfo){
    vector<int> ShardAndGroupId;
    string tmp = "";
    for(int i = 0; i < shardAndGroupIdInfo.size(); i++){
        if(shardAndGroupIdInfo[i] != ':'){
            tmp += shardAndGroupIdInfo[i];
        }else{
            if(tmp.size() != 0){
                ShardAndGroupId.push_back(stoi(tmp));
                tmp = "";
            }
        }
    }
    return ShardAndGroupId;
}

class MoveReply{
public:
    bool isWrongLeader;
};

class QueryArgs{
public:
    int configNum;
    int clientId;
    int requestId;
};

class QueryReply{
public:
    Config getConfig();
    string configStr;
    bool isWrongLeader;
    // Config config;
    friend Serializer& operator >> (Serializer& in, QueryReply& d) {
		in >> d.configStr >> d.isWrongLeader;
		return in;
	}
	friend Serializer& operator << (Serializer& out, QueryReply d) {
		out << d.configStr << d.isWrongLeader;
		return out;
	}
};

string getStringFromConfig(Config config){
    string str;
    str += to_string(config.configNum) + "/";
    for(const auto& shard : config.shards){
        str += to_string(shard) + ":";
    }
    str += "/";
    for(const auto& group : config.groups){
        str += to_string(group.first) + ":";
        for(const auto& v : group.second){
            str += v + ":";
        }
        str += "|";
    }
    str += "/";
    return str;
}

Config QueryReply::getConfig(){
    Config config;
    vector<string> str;
    string tmp = "";
    // printf("configStr is %s\n", configStr.c_str());
    for(int i = 0; i < configStr.size(); i++){
        if(configStr[i] != '/'){
            tmp += configStr[i];
        }else{
            if(tmp.size() != 0){
                str.push_back(tmp);
                tmp = "";
            }
        }
    }
    if(str.size() == 2){
        config.configNum = stoi(str[0]);
        config.shards = GetVectorOfIntFromString(str[1]);
        config.groups.clear();
        return config;
    }
    config.configNum = stoi(str[0]);
    config.shards = GetVectorOfIntFromString(str[1]);
    config.groups = getMapFromServersShardInfo(str[2]);
    return config;
}

#endif