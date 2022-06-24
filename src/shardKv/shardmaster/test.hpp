#include "common.h"

/**
 * @brief 用于测试各个args的转换逆转换函数以及实际负载均衡的效果，均OK
 * 
 */

void testJOinArgs(){
    JoinArgs args;
    unordered_map<int, vector<string>> servers;
    servers[1] = vector<string>{"ab", "bc", "cd"};
    servers[2] = vector<string>{"de", "ef", "fg"};
    args.getServersShardInfoFromMap(servers);

    unordered_map<int, vector<string>> tmp = getMapFromServersShardInfo(args.serversShardInfo);
    for(auto a : tmp){
        cout<<"idx : "<<a.first<<" -> ";
        for(auto v : a.second){
            cout<<v<<" ";
        }
        cout<<endl;
    }
}

void testLeaveArgs(){
    LeaveArgs args;
    vector<int> ids{1, 2, 3};
    args.getGroupIdsInfoFromVector(ids);

    vector<int> tmp = GetVectorOfIntFromString(args.groupIdsInfo);
    for(auto a : tmp){
        cout<<a<<endl;
    }
}

void testMoveArgs(){
    MoveArgs args;
    args.getStringOfShardAndGroupId(3, 5);

    vector<int> tmp = getShardAndGroupId(args.shardAndGroupIdInfo);
    cout<<"shard : "<<tmp[0]<<endl;
    cout<<"gid "<<tmp[1]<<endl;
}

void printConfig(Config config){
    cout<<"begin print -----------------------------------------------"<<endl;
    cout<<"configNum is "<<config.configNum<<endl;
    cout<<"groups.size() is :"<<config.groups.size()<<endl;
    for(auto a : config.groups){
        cout<<"idx : "<<a.first<<" -> ";
        for(auto v : a.second){
            cout<<v<<" ";
        }
        cout<<endl;
    }
    cout<<"shards is ";
    for(auto a : config.shards){
        cout<<a<<" ";
    }
    cout<<endl;
    cout<<"end print -----------------------------------------------"<<endl;
}

void testQueryReply(){
    QueryReply reply;
    Config config;
    config.groups[1] = vector<string>{"ab", "bc", "cd"};
    config.groups[2] = vector<string>{"de", "ef", "fg"};
    cout<<"before tranform :"<<endl;
    printConfig(config);
    reply.configStr = getStringFromConfig(config);

    cout<<endl;

    Config newConfig = reply.getConfig();
    printConfig(newConfig);
    cout<<endl;
}

void testRpc(){
    cout<<"testJOinArgs------------------------------"<<endl;
    testJOinArgs();
    cout<<endl;
    cout<<"testLeaveArgs------------------------------"<<endl;
    testLeaveArgs();
    cout<<endl;
    cout<<"testMoveArgs------------------------------"<<endl;
    testMoveArgs();
    cout<<endl;
    cout<<"testQueryReply------------------------------"<<endl;
    testQueryReply();
}

class mycmpLower{
public:
    bool operator()(const pair<int, vector<int>>& a, const pair<int, vector<int>>& b){
        return a.second.size() > b.second.size();
    }
};

class mycmpUpper{
public:
    bool operator()(const pair<int, vector<int>>& a, const pair<int, vector<int>>& b){
        return a.second.size() < b.second.size();
    }
};

typedef priority_queue<pair<int, vector<int>>, vector<pair<int, vector<int>>>, mycmpLower> lowSizeQueue;
typedef priority_queue<pair<int, vector<int>>, vector<pair<int, vector<int>>>, mycmpUpper> upSizeQueue;

void syncLowerLoadSize(unordered_map<int, vector<int>>& workLoad, lowSizeQueue& lowerLoadSize){
    while(!lowerLoadSize.empty()){
        lowerLoadSize.pop();
    }
    for(const auto& load : workLoad){
        lowerLoadSize.push(load);
    }
}   

void syncUpperLoadSize(unordered_map<int, vector<int>>& workLoad, upSizeQueue& upperLoadSize){
    while(!upperLoadSize.empty()){
        upperLoadSize.pop();
    }
    for(const auto& load : workLoad){
        upperLoadSize.push(load);
    }
}

void balanceWorkLoad(Config& config){
    lowSizeQueue lowerLoadSize;
    unordered_map<int, vector<int>> workLoad;
    for(const auto& group : config.groups){
        workLoad[group.first] = vector<int>{};
    }
    for(int i = 0; i < config.shards.size(); i++){
        if(config.shards[i] != 0){
            workLoad[config.shards[i]].push_back(i);
        }
    }
    syncLowerLoadSize(workLoad, lowerLoadSize);
    for(int i = 0; i < config.shards.size(); i++){
        if(config.shards[i] == 0){
            auto load = lowerLoadSize.top();     //找负载最小的组
            lowerLoadSize.pop();
            workLoad[load.first].push_back(i);
            load.second.push_back(i);
            lowerLoadSize.push(load);
        }
    }
    upSizeQueue upperLoadSize;
    syncUpperLoadSize(workLoad, upperLoadSize);
    if(NShards % config.groups.size() == 0){
        while(lowerLoadSize.top().second.size() != upperLoadSize.top().second.size()){
            workLoad[lowerLoadSize.top().first].push_back(upperLoadSize.top().second.back());
            workLoad[upperLoadSize.top().first].pop_back();
            syncLowerLoadSize(workLoad, lowerLoadSize);
            syncUpperLoadSize(workLoad, upperLoadSize);
        }
    }else{
        while(upperLoadSize.top().second.size() - lowerLoadSize.top().second.size() > 1){
            workLoad[lowerLoadSize.top().first].push_back(upperLoadSize.top().second.back());
            workLoad[upperLoadSize.top().first].pop_back();
            syncLowerLoadSize(workLoad, lowerLoadSize);
            syncUpperLoadSize(workLoad, upperLoadSize);
        }
    }
    for(const auto& load : workLoad){
        for(const auto& idx : load.second){
            config.shards[idx] = load.first;
        }
    }
}

void testBalanceWorkLoad(){
    Config config;
    config.shards = {1, 1, 1, 2, 2, 3, 2, 3, 3, 3};
    // config.shards = {1, 1, 1, 2, 2, 0, 2, 0, 0, 0};
    config.groups[1] = vector<string>{"a", "b"};
    config.groups[2] = vector<string>{"c", "d"};
    config.groups[3] = vector<string>{"e", "f"};
    config.groups[4] = vector<string>{"m"};
    config.groups[5] = vector<string>{"q"};
    // config.groups[6] = vector<string>{"q"};
    printConfig(config);

    cout<<"---------------------"<<endl;

    balanceWorkLoad(config);
    printConfig(config);
}

// int main(){
//     // testRpc();
//     testBalanceWorkLoad();
// }