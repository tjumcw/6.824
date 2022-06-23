#include "raft.hpp"
#include <bits/stdc++.h>
#include "common.h"
using namespace std;

// class ServerInfo{
// public:
//     int port;
//     int id;
// };

#define EVERY_SERVER_PORT 3
#define EVERY_SERVER_RAFT 5
#define MASTER_PORT 6666

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

class shardClerk{
public:
    static int cur_portId;
    static locker port_lock;
    void makeClerk(vector<vector<int>>& serverPorts);
    void Join(unordered_map<int, vector<string>>& servers);
    Config Query(int num);
    void Leave(vector<int>& gIds);
    void Move(int shard, int gId);
    int getCurRequestId();
    int getCurLeader();
    int getChangeLeader();

private:
    locker m_requestId_lock;
    vector<vector<int>> serverPorts;
    int leaderId;
    int clientId;
    int requestId;
};
int shardClerk::cur_portId = 0;
locker shardClerk::port_lock;

void shardClerk::makeClerk(vector<vector<int>>& serverPorts){
    // srand((unsigned)time(NULL));    //里面不能设置随机种子，在main中初始化随机种子
    this->serverPorts = serverPorts;
    this->clientId = rand() % 100000;
    printf("clientId is %d\n", clientId);
    this->requestId = 0;
    this->leaderId = rand() % serverPorts.size();
}

int shardClerk::getCurRequestId(){        //封装成原子操作，避免每次加解锁，代码复用
    m_requestId_lock.lock();
    int cur_requestId = requestId++;
    m_requestId_lock.unlock();
    return cur_requestId;
}

int shardClerk::getCurLeader(){
    m_requestId_lock.lock();
    int cur_leader = leaderId;
    m_requestId_lock.unlock();
    return cur_leader;
}

int shardClerk::getChangeLeader(){
    m_requestId_lock.lock();
    leaderId = (leaderId + 1) % serverPorts.size();
    int new_leader  = leaderId;
    m_requestId_lock.unlock();
    return new_leader;
}

void shardClerk::Join(unordered_map<int, vector<string>>& servers){
    JoinArgs args;
    args.getServersShardInfoFromMap(servers);
    args.requestId = getCurRequestId();
    args.clientId = clientId;
    int cur_leader = getCurLeader();

    port_lock.lock();
    int curPort = (cur_portId++) % EVERY_SERVER_PORT;
    // printf("in %ld Join port is %d\n", pthread_self(), serverPorts[cur_leader][curPort]);
    port_lock.unlock();

    while(1){
        buttonrpc client;
        client.as_client("127.0.0.1", serverPorts[cur_leader][curPort]);
        JoinReply reply = client.call<JoinReply>("Join", args).val();
        if(!reply.isWrongLeader){
            printf("Join is finished, port is %d, leader is %d\n", serverPorts[cur_leader][curPort], cur_leader);
            return;
        }
        // printf("in join clerk%d's leader %d is wrong\n", clientId, cur_leader);
        cur_leader = getChangeLeader();
        usleep(100000);
    }   
}

Config shardClerk::Query(int num){
    QueryArgs args;
    args.configNum = num;
    args.clientId = clientId;
    args.requestId = getCurRequestId();
    int cur_leader = getCurLeader();

    port_lock.lock();
    int curPort = (cur_portId++) % EVERY_SERVER_PORT;
    // printf("in %ld Query port is %d\n", pthread_self(), serverPorts[cur_leader][curPort]);
    port_lock.unlock();

    while(1){
        buttonrpc client;
        client.as_client("127.0.0.1", serverPorts[cur_leader][curPort]);
        QueryReply reply = client.call<QueryReply>("Query", args).val();
        if(!reply.isWrongLeader){
            // printf("Query is finished, port is %d, leader is %d\n", serverPorts[cur_leader][curPort], cur_leader);
             Config retCfg = reply.getConfig();
            //  printConfig(retCfg);
             return retCfg;
        }
        // printf("in query clerk%d's leader %d's port : %d is wrong\n", clientId, cur_leader, serverPorts[cur_leader][curPort]);
        cur_leader = getChangeLeader();
        usleep(100000);
    }   
}

vector<vector<int>> getMastersPort(int num){
    vector<vector<int>> kvServerPort(num);
    for(int i = 0; i < num; i++){
        for(int j = 0; j < EVERY_SERVER_PORT; j++){
            kvServerPort[i].push_back(MASTER_PORT + i + (j + 2) * num);
        }
    }
    return kvServerPort;
}

vector<vector<int>> getMastersBehindPort(int num){
    vector<vector<int>> kvServerPort(num);
    for(int i = 0; i < num; i++){
        for(int j = 0; j < EVERY_SERVER_PORT; j++){
            kvServerPort[i].push_back(MASTER_PORT + i + (j + 2) * num + EVERY_SERVER_PORT * num);
        }
    }
    return kvServerPort;
}

void shardClerk::Leave(vector<int>& gIds){
    LeaveArgs args;
    args.getGroupIdsInfoFromVector(gIds);
    args.clientId = clientId;
    args.requestId = getCurRequestId();
    int cur_leader = getCurLeader();

    port_lock.lock();
    int curPort = (cur_portId++) % EVERY_SERVER_PORT;
    // printf("in %ld Leave port is %d\n", pthread_self(), serverPorts[cur_leader][curPort]);  
    port_lock.unlock();

    while(1){
        buttonrpc client;
        client.as_client("127.0.0.1", serverPorts[cur_leader][curPort]);
        LeaveReply reply = client.call<LeaveReply>("Leave", args).val();
        if(!reply.isWrongLeader){
            return;
        }
        printf("in leave clerk%d's leader %d is wrong\n", clientId, cur_leader);
        cur_leader = getChangeLeader();
        usleep(100000);
    }   
}

void shardClerk::Move(int shard, int gId){
    MoveArgs args;
    args.clientId = clientId;
    args.requestId = getCurRequestId();
    args.getStringOfShardAndGroupId(shard, gId);
    int cur_leader = getCurLeader();

    port_lock.lock();
    int curPort = (cur_portId++) % EVERY_SERVER_PORT;
    // printf("in %ld Move port is %d\n", pthread_self(), serverPorts[cur_leader][curPort]);
    port_lock.unlock();

    while(1){
        buttonrpc client;
        client.as_client("127.0.0.1", serverPorts[cur_leader][curPort]);
        MoveReply reply = client.call<MoveReply>("Move", args).val();
        if(!reply.isWrongLeader){
            return;
        }
        printf("in move clerk%d's leader %d is wrong\n", clientId, cur_leader);
        cur_leader = getChangeLeader();
        usleep(100000);
    }   
} 

