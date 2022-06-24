#include "myClass.h"
using namespace std;

/**
 * @brief Clerk类封装有一个shardClerk，用于更新配置和获取配置信息，其本身有get、put、append的操作，相当于封装了
 * 具有shard功能的一个kvClerk，理解成具有分片功能的LAB3的客户端类，也是类似其功能和框架，对照理解即可
 */

unordered_map<string, int> str2Port;    //一个用于通过server的名字获取其port的map

//初始化str2Port，要在main函数构造Clerk前调用，才能再Clerk启动后正确得到端口
void initStr2PortMap(int num){
    if(num >= 10 || num <= 0){
        exit(-1);
    }
    for(int i = 1; i <= num; i++){
        for(int j = 1; j <= EVERY_SERVER_RAFT; j++){
            string str = to_string(i) + to_string(j);
            str2Port[str] = COMMOM_PORT + 2 * EVERY_SERVER_RAFT + j - 1 + (i - 1) * 100;
            // printf("%d ", COMMOM_PORT + 2 * EVERY_SERVER_RAFT + j - 1 + (i - 1) * 100);
        }
        // printf("\n");
    }
}

//通过make_end可以使serverName -> port
int make_end(string str){
    return str2Port[str];
}

//获取key到对应分片的hash函数
int key2shard(string key){
    int shard = 0;
    if(key.size() > 0){
        shard = key[0] - 'a';
    }
    shard = shard % NShards;
    return shard;
}

class Clerk{
public:
    Clerk(vector<vector<int>>& servers);
    string get(string key);
    void put(string key, string value);
    void append(string key, string value);
    void putAppend(string key, string value, string op);

    //--------------------用于测试时更新配置--------------------
    void Join(unordered_map<int, vector<string>>& servers) { masterClerk.Join(servers); }
    void Move(int shard, int gId) { masterClerk.Move(shard, gId); }
    void Leave(vector<int>& gIds) { masterClerk.Leave(gIds); }
    Config Query(int num) { return masterClerk.Query(num); }

private:
    shardClerk masterClerk;             
    Config config;                          //用于获取配置，若找不到对应的gid就会通过Query()更新配置

    unordered_map<int, int> leaderId;
    int clientId;
    int requestId;
};

Clerk::Clerk(vector<vector<int>>& servers){
    masterClerk.makeClerk(servers);
    this->clientId = rand() % 10000 + 1;
    this->requestId = 0;
    this->leaderId.clear();
}

string Clerk::get(string key){
    GetArgs args;
    args.key = key;
    args.clientId = clientId;
    args.requestId = requestId++;

    while(1){
        int shard = key2shard(key);
        int gid = config.shards[shard];
        if(config.groups.count(gid)){
            printf("in get, key : %s -> gid is %d\n", key.c_str(), gid);
            vector<string> servers = config.groups[gid];
            for(int i = 0; i < servers.size(); i++){
                int curLeader = (i + leaderId[gid]) % servers.size();
                int port = make_end(servers[curLeader]);
                buttonrpc client;
                client.as_client("127.0.0.1", port);
                // printf("get is called, servers[curLeader] is %s, port is %d\n", servers[curLeader].c_str(), port);
                GetReply reply = client.call<GetReply>("get", args).val();
                if(reply.err == OK || reply.err == ErrNoKey){
                    leaderId[gid] = curLeader;
                    // printf("get is finished to gid%d, leaderPort is %d\n", gid, port);
                    return reply.value;
                }
                if(reply.err == ErrWrongGroup){
                    break;
                }
            }
        }   
        usleep(100000);
        config = masterClerk.Query(-1);    
    }   
}

void Clerk::put(string key, string value){
    putAppend(key, value, "put");
}

void Clerk::append(string key, string value){
    putAppend(key, value, "append");
}

void Clerk::putAppend(string key, string value, string op){
    PutAppendArgs args;
    args.key = key;
    args.value = value;
    args.op = op;
    args.clientId = clientId;
    args.requestId = requestId++;

    while(1){
        int shard = key2shard(key);
        int gid = config.shards[shard];
        printf("in put key is %s, gid is %d\n", key.c_str(), gid);
        if(config.groups.count(gid)){
            vector<string> servers = config.groups[gid];
            for(int i = 0; i < servers.size(); i++){
                int curLeader = (i + leaderId[gid]) % servers.size();
                int port = make_end(servers[curLeader]);
                buttonrpc client;
                client.as_client("127.0.0.1", port);
                // printf("put is called, servers[curLeader] is %s, port is %d\n", servers[curLeader].c_str(), port);
                PutAppendReply reply = client.call<PutAppendReply>("putAppend", args).val();
                if(reply.err == OK){
                    leaderId[gid] = curLeader;
                    // printf("putAppend is finished to gid%d, leaderPort is %d\n", gid, port);
                    return;
                }
                if(reply.err == ErrWrongGroup){
                    break;
                }
            }
        }
        usleep(100000);
        config = masterClerk.Query(-1);    
        // printConfig(config);
    }   
}


unordered_map<int, vector<string>> getJoinArgs(vector<int> gids){
    unordered_map<int, vector<string>> ret;
    for(int i = 0; i < gids.size(); i++){
        if(gids[i] <= 0 || gids[i] > 5){
            printf("ERROR : the gid between 1 ~ 5\n");
            exit(-1);
        }
        for(int j = 1; j <= EVERY_SERVER_RAFT; j++){
            string tmp = to_string(gids[i]) + to_string(j);
            ret[gids[i]].push_back(tmp);
        }
    }
    return ret;
}

int main(){
    srand((unsigned)time(NULL));
    initStr2PortMap(5);
    vector<vector<int>> port = getMastersBehindPort(EVERY_SERVER_RAFT);
    // for(int i = 0; i < port.size(); i++){
    //     for(int j = 0; j < port[i].size(); j++){
    //         cout<<port[i][j]<<" ";
    //     }
    //     cout<<endl;
    // }
    Clerk clerk(port);

    //--------------------------------以下均为测试------------------------------
    Config config = clerk.Query(-1);
    printConfig(config);
    unordered_map<int, vector<string>> a = getJoinArgs(vector<int>{2});
    clerk.Join(a);

    // config = clerk.Query(-1);
    // printf("begin ```````````````````````````````````\n");
    // printConfig(config);
    // printf("end ***********************************\n");
    clerk.put("e", "4");
    cout<<clerk.get("e")<<endl;
    clerk.append("f", "5");
    cout<<clerk.get("f")<<endl;
    clerk.append("f", "5");
    cout<<clerk.get("f")<<endl;
    clerk.put("g", "6");
    cout<<clerk.get("g")<<endl;
    clerk.put("h", "7");
    cout<<clerk.get("h")<<endl;
    clerk.put("i", "8");
    cout<<clerk.get("i")<<endl;
    clerk.put("j", "9");
    cout<<clerk.get("j")<<endl;

    unordered_map<int, vector<string>> b = getJoinArgs(vector<int>{1, 3});
    clerk.Join(b);
    config = clerk.Query(-1);
    printConfig(config);
    sleep(1);
    for(char i = 'e'; i <= 'j'; i++){
        string str = "";
        str += i;
        cout<<clerk.get(str)<<endl;
    }

    clerk.put("a", "0");
    cout<<clerk.get("a")<<endl;
    clerk.append("b", "1");
    cout<<clerk.get("b")<<endl;
    clerk.append("c", "2");
    cout<<clerk.get("c")<<endl;
    clerk.put("d", "3");
    cout<<clerk.get("d")<<endl;

    unordered_map<int, vector<string>> c = getJoinArgs(vector<int>{4, 5});
    clerk.Join(c);
    config = clerk.Query(-1);
    printConfig(config);

    sleep(1);
    for(char i = 'a'; i <= 'j'; i++){
        string str = "";
        str += i;
        cout<<clerk.get(str)<<endl;
    }

    vector<int> d{2, 3, 4};
    clerk.Leave(d);
    config = clerk.Query(-1);
    printConfig(config);

    sleep(1);
    for(char i = 'a'; i <= 'j'; i++){
        string str = "";
        str += i;
        cout<<clerk.get(str)<<endl;
    }


    //Move应该是基于当前config中存在的gid进行move，而将某个shardMove给不存在的gid是不对的，
    //那是join的工作，Move只是改变当前config内的gid布局，不会修改config的groups
    clerk.Move(0, 1);
    clerk.Move(2, 1);
    clerk.Move(3, 1);
    config = clerk.Query(-1);
    printConfig(config);

    sleep(1);
    for(char i = 'a'; i <= 'j'; i++){
        string str = "";
        str += i;
        cout<<clerk.get(str)<<endl;
    }

    unordered_map<int, vector<string>> e = getJoinArgs(vector<int>{2, 3, 4});
    clerk.Join(e);
    config = clerk.Query(-1);
    printConfig(config);

    sleep(1);
    for(char i = 'a'; i <= 'j'; i++){
            string str = "";
            str += i;
            cout<<clerk.get(str)<<endl;
    }

    // while(1){
    //     for(char i = 'a'; i <= 'j'; i++){
    //         string str = "";
    //         str += i;
    //         cout<<clerk.get(str)<<endl;
    //     }
    //     usleep(500000);
    // }

}