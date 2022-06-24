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

/**
 * @brief 代码基本结构类似LAB3的client.cpp，但是内容完全不一样了，实现的功能也差异较大
 * 一些注释可以看LAB3的代码，许多辅助类的定义和功能都是一样的如select
 */

//写的打印config的函数，用于调试代码及测试程序效果
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
    static int cur_portId;          //类似LAB3中的cur_portId，用于轮询对应server的端口(设置了多个端口监听相同请求)
    static locker port_lock;        //但由于封装成了hpp(LAB4B需要类内复合)，没法直接用全局变量，那就用类的static变量
    void makeClerk(vector<vector<int>>& serverPorts);           //初始化函数，不写成构造是因为需要在复合它的类中传递参数，不好调其构造函数
    void Join(unordered_map<int, vector<string>>& servers);     //向客户端发起Join请求，加入新的集群(gid -> [servers])，需更新配置
    Config Query(int num);                                      //向客户端发起Query请求，获取特定编号的config配置信息
    void Leave(vector<int>& gIds);                              //向客户端发起Leave请求，移除特定的集群，需更新配置信息
    void Move(int shard, int gId);                              //向客户端发起Move请求，将当前配置内有的gid做Move(必须有，没有的话用Join)，需更新配置
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
    args.getServersShardInfoFromMap(servers);   //将map信息转化到类内的string中使得RPC能够正确传输
    args.requestId = getCurRequestId();
    args.clientId = clientId;
    int cur_leader = getCurLeader();

    port_lock.lock();
    int curPort = (cur_portId++) % EVERY_SERVER_PORT;       //取得server端监听相同请求的任一port，避免同一port排队处理过慢，其他类似不注释
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

//获得用于管理分片的shardMaster的端口信息，但是是前面的端口。
//为了后续LAB4B中server封装的shardClerk和普通发请求的clerk内的shardClerk获得不干扰的端口
vector<vector<int>> getMastersPort(int num){
    vector<vector<int>> kvServerPort(num);
    for(int i = 0; i < num; i++){
        for(int j = 0; j < EVERY_SERVER_PORT; j++){
            kvServerPort[i].push_back(MASTER_PORT + i + (j + 2) * num);
        }
    }
    return kvServerPort;
}

//获得用于管理分片的shardMaster的端口信息，但是是后面的端口。
//为了后续LAB4B中server封装的shardClerk和普通发请求的clerk内的shardClerk获得不干扰的端口
vector<vector<int>> getMastersBehindPort(int num){
    vector<vector<int>> kvServerPort(num);
    for(int i = 0; i < num; i++){
        for(int j = 0; j < EVERY_SERVER_PORT; j++){
            kvServerPort[i].push_back(MASTER_PORT + i + (j + 2) * num + EVERY_SERVER_PORT * num);
        }
    }
    return kvServerPort;
}