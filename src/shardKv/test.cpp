#include <bits/stdc++.h>
#include "myClass.h"
using namespace std;

string MigrateReply2Str(MigrateReply reply){
    string str;
    str += to_string(reply.configNum) + "|" + to_string(reply.shard) + "|" + reply.err + "|";
    for(const auto& req : reply.clientReqId){
        str += to_string(req.first) + ":" + to_string(req.second) + "/";
    }
    str += "|";
    for(const auto& data : reply.database){
        str += data.first + ":" + data.second + "/";
    }
    str += "|";
    return str;
}

MigrateReply str2MigrateReply(string str){
    MigrateReply reply;
    vector<string> content;
    string tmp; 
    for(int i = 0; i < str.size(); i++){
        if(str[i] != '|'){
            tmp += str[i];
        }else{
            if(tmp.size() != 0){
                content.push_back(tmp);
                tmp = "";
            }
        }
    }
    reply.configNum = stoi(content[0]);
    reply.shard = stoi(content[1]);
    reply.err = content[2];
    vector<string> request;
    tmp = "";
    for(int i = 0; i < content[3].size(); i++){
        if(content[3][i] != '/'){
            tmp += content[3][i];
        }else{
            if(tmp.size() != 0){
                request.push_back(tmp);
                tmp = "";
            }
        }
    }
    for(int i = 0; i < request.size(); i++){
        tmp = "";
        int j = 0;
        for(; j < request[i].size(); j++){
            if(request[i][j] != ':'){
                tmp += request[i][j];
            }else break;
        }
        string number(request[i].begin() + j + 1, request[i].end());
        reply.clientReqId[stoi(tmp)] = stoi(number);
    }
    tmp = "";
    vector<string> data;
    for(int i = 0; i < content[4].size(); i++){
        if(content[4][i] != '/'){
            tmp += content[4][i];
        }else{
            if(tmp.size() != 0){
                data.push_back(tmp);
                tmp = "";
            }
        }
    }
    for(int i = 0; i < data.size(); i++){
        tmp = "";
        int j = 0;
        for(; j < data[i].size(); j++){
            if(data[i][j] != ':'){
                tmp += data[i][j];
            }else break;
        }
        string value(data[i].begin() + j + 1, data[i].end());
        reply.database[tmp] = value;
    }
    return reply;
}

void printMigrateReply(MigrateReply reply){
    printf("err is %s, configNum is %d, shard is %d\n", reply.err.c_str(), reply.configNum, reply.shard);
    for(auto req : reply.clientReqId){
        printf("%d -> %d;", req.first, req.second);
    }
    printf("\n");
    for(auto data : reply.database){
        printf("%s -> %s;", data.first.c_str(), data.second.c_str());
    }
    printf("\n");
}

void testMigrate2str(){
    MigrateReply reply;
    reply.err = OK;
    reply.configNum = 1;
    reply.shard = 3;
    unordered_map<int, int> req;
    req[1] = 1;
    req[2] = 2;
    req[3] = 5;
    reply.clientReqId = req;
    unordered_map<string, string> data;
    data["abc"] = "123";
    data["bcd"] = "234";
    data["cde"] = "345";
    reply.database = data;

    string str = MigrateReply2Str(reply);
    cout<<str<<endl;

    MigrateReply newReply = str2MigrateReply(str);
    printMigrateReply(newReply);

}

class ShardKv{
public:
    static void* RPCserver(void* arg);
    static void* applyLoop(void* arg);
    static void* snapShotLoop(void* arg);
    static void* updateConfigLoop(void* arg);
    static void* pullShardLoop(void* arg);
    static void* garbagesCollectLoop(void* arg);
    static void* doGarbage(void* arg);
    static void* doPullShard(void* arg);

    void StartKvServer(vector<kvServerInfo>& kvInfo, int me, int maxRaftState);
    void getPutAppendOnDataBase(ShardKv* kv, Operation operation, OpContext* opctx, bool isOpExist, bool isKeyExisted, int prevRequestIdx);
    void doSnapShot(ShardKv* kv, ApplyMsg msg);

    GetReply get(GetArgs args);
    PutAppendReply putAppend(PutAppendArgs args);
    MigrateReply shardMigration(MigrateArgs args);
    GarbagesCollectReply garbagesCollect(GarbagesCollectArgs args);

    bool isMatchShard(string key);
    void updateComeInAndOutShrads(Config config);
    void updateDataBaseWithMigrateReply(MigrateReply reply);
    void clearToOutData(int cfgNum, int shard);
    // string test(string key){ return m_database[key]; }  //测试其余不是leader的server的状态机

    string getSnapShot();
    void recoverySnapShot(string snapShot);
    void printSnapShot();


    locker m_lock;
    Raft m_raft;
    int m_id;
    int m_port;
    int m_groupId;

    int m_maxraftstate;  //超过这个大小就快照
    int m_lastAppliedIndex;

    shardClerk m_masterClerk;
    Config m_config;

    unordered_map<string, string> m_database;  //模拟数据库
    unordered_map<int, int> m_clientSeqMap;    //只记录特定客户端已提交的最大请求ID的去重map，不需要针对分片，迁移时整个发送即可
    unordered_map<int, OpContext*> m_requestMap;  //记录当前RPC对应的上下文

    unordered_map<int, unordered_map<int, unordered_map<string, string>>> toOutShards;
    unordered_map<int, int> comeInShards;
    unordered_set<int> m_AvailableShards;
    unordered_map<int, unordered_map<int, bool>> garbages;
    unordered_map<int, unordered_map<int, bool>> garbagesBackUp;

    int garbageSize;
    int garbageFinished;
    int garbageConfigNum;
    int pullShardSize;
    int pullShardFinished;

    unordered_map<int, bool>::iterator garbageIter;
    unordered_map<int, int>::iterator pullShardIter;

};

Config getConfig(string configStr){
    Config config;
    vector<string> str;
    string tmp = "";
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

string ShardKv::getSnapShot(){
    string snapShot;
    if(m_database.empty()) snapShot += "empty";
    else{
        for(const auto& ele : m_database){
            snapShot += ele.first + " " + ele.second + ".";
        }
    }
    snapShot += ";";
    if(m_clientSeqMap.empty()) snapShot += "empty";
    else{
        for(const auto& ele : m_clientSeqMap){
            snapShot += to_string(ele.first) + " " + to_string(ele.second) + ".";
        }
    }
    snapShot += ";";
    if(comeInShards.empty()) snapShot += "empty";
    else{
        for(const auto& shard : comeInShards){
            snapShot += to_string(shard.first) + " " + to_string(shard.second) + ".";
        }
    }
    snapShot += ";";
    if(toOutShards.empty()) snapShot += "empty";
    else{
        for(const auto& cfg2shardDB : toOutShards){
            snapShot += to_string(cfg2shardDB.first) + ":";
            for(const auto& shardDB : cfg2shardDB.second){
                snapShot += to_string(shardDB.first) + ",";
                for(const auto& data : shardDB.second){
                    snapShot += data.first + " " + data.second + ".";
                }
                snapShot += "/";
            }
            snapShot += "|";
        }
    }
    snapShot += ";";
    if(m_AvailableShards.empty()) snapShot += "empty";
    else{
        for(const auto& shard : m_AvailableShards){
            snapShot += to_string(shard) + ".";
        }
    }
    snapShot += ";";
    if(garbages.empty()) snapShot += "empty";
    else{
        for(const auto& garbage : garbages){
            snapShot += to_string(garbage.first) + ":";
            for(const auto& shard : garbage.second){
                snapShot += to_string(shard.first) + ".";
            }
            snapShot += "|";
        }
    }
    snapShot += ";";
    snapShot += getStringFromConfig(this->m_config) + ";";
    // cout<<"int cout snapShot is "<<snapShot<<endl;
    // printf("in kvserver -----------------snapShot is %s\n", snapShot.c_str());
    return snapShot;
}

vector<string> splitStr(string str, char op){
    vector<string> ret;
    string tmp = "";
    for(int i = 0; i < str.size(); i++){
        if(str[i] != op){
            tmp += str[i];
        }else{
            if(tmp.size() != 0){
                ret.push_back(tmp);
                tmp = "";
            }
        }
    }
    if(tmp.size() != 0) ret.push_back(tmp);
    return ret;
}

void ShardKv::recoverySnapShot(string snapShot){
    printf("recovery is called\n");
    vector<string> str = splitStr(snapShot, ';');
    string tmp = "";
    if(str[0] == "empty") m_database.clear();
    else{
        vector<string> kvData = splitStr(str[0], '.');
        for(int i = 0; i < kvData.size(); i++){
            tmp = "";
            int j = 0;
            for(; j < kvData[i].size(); j++){
                if(kvData[i][j] != ' '){
                    tmp += kvData[i][j];
                }else break;
            }
            string value(kvData[i].begin() + j + 1, kvData[i].end());
            m_database[tmp] = value;
        }
    }

    tmp = "";
    if(str[1] == "empty") m_clientSeqMap.clear();
    else{
        vector<string> clientSeq = splitStr(str[1], '.');
        for(int i = 0; i < clientSeq.size(); i++){
            tmp = "";
            int j = 0;
            for(; j < clientSeq[i].size(); j++){
                if(clientSeq[i][j] != ' '){
                    tmp += clientSeq[i][j];
                }else break;
            }
            string value(clientSeq[i].begin() + j + 1, clientSeq[i].end());
            m_clientSeqMap[stoi(tmp)] = stoi(value);
        }
    }

    tmp = "";
    if(str[2] == "empty") comeInShards.clear();
    else{
        vector<string> inShards = splitStr(str[2], '.');
        for(int i = 0; i < inShards.size(); i++){
            tmp = "";
            int j = 0;
            for(; j < inShards[i].size(); j++){
                if(inShards[i][j] != ' '){
                    tmp += inShards[i][j];
                }else break;
            }
            string value(inShards[i].begin() + j + 1, inShards[i].end());
            comeInShards[stoi(tmp)] = stoi(value);
        }
    }
    tmp = "";
    if(str[3] == "empty") toOutShards.clear();
    else{
        vector<string> cfg2Shards = splitStr(str[3], '|');
        for(int i = 0; i < cfg2Shards.size(); i++){
            vector<string> cfgAndData = splitStr(cfg2Shards[i], ':');
            int cfgNum = stoi(cfgAndData[0]);
            for(int j = 1; j < cfgAndData.size(); j++){
                vector<string> shardData = splitStr(cfgAndData[j], '/');
                for(int k = 0; k < shardData.size(); k++){
                    vector<string> data = splitStr(shardData[k], ',');
                    int shard = stoi(data[0]);
                    vector<string> key2Value = splitStr(data[1], '.');
                    for(int ii = 0; ii < key2Value.size(); ii++){
                        tmp = "";
                        int jj = 0;
                        for(; jj < key2Value[ii].size(); jj++){
                            if(key2Value[ii][jj] != ' '){
                                tmp += key2Value[ii][jj];
                            }else break;
                        }
                        string value(key2Value[ii].begin() + jj + 1, key2Value[ii].end());
                        toOutShards[cfgNum][shard][tmp] = value;
                    }
                }
            }
        }
    }

    tmp = "";
    if(str[4] == "empty") m_AvailableShards.clear();
    else{
        vector<string> shards = splitStr(str[4], '.');
        for(int i = 0; i < shards.size(); i++){
            m_AvailableShards.insert(stoi(shards[i]));
        }
    }

    tmp = "";
    if(str[5] == "empty") garbages.clear();
    else{
        vector<string> cfg2garbage = splitStr(str[5], '|');
        for(int i = 0; i < cfg2garbage.size(); i++){
            tmp = "";
            int j = 0;
            for(; j < cfg2garbage[i].size(); j++){
                if(cfg2garbage[i][j] != ':'){
                    tmp += cfg2garbage[i][j];
                }else break;
            }
            string shard(cfg2garbage[i].begin() + j + 1, cfg2garbage[i].end());
            int cfgNum = stoi(tmp);
            vector<string> shards = splitStr(shard, '.');
            for(int k = 0; k < shards.size(); k++){
                garbages[cfgNum][stoi(shards[k])] = true;
            }
        }
    }

    this->m_config = getConfig(str[6]);
}

void ShardKv::printSnapShot(){
    printf("-----------------databegin---------------------------\n");
    for(auto a : m_database){
        printf("data-> key is %s, value is %s\n", a.first.c_str(), a.second.c_str());
    }
    printf("-----------------requSeqbegin-------------------------\n");
    for(auto a : m_clientSeqMap){
        printf("data-> key is %d, value is %d\n", a.first, a.second);
    }
    printf("-----------------comeInshards-------------------------\n");
    for(auto a : comeInShards){
        printf("shard : %d, configNum is %d\n", a.first, a.second);
    }
    printf("---------------toOutShards----------------------------\n");
    for(auto a : toOutShards){
        for(auto b : a.second){
            for(auto c : b.second){
                printf("in config%d, the shard%d's data -> key is %s, value is %s\n",a.first, b.first, c.first.c_str(), c.second.c_str());
            }
        }
    }
    printf("------------------availableShards---------------------\n");
    printf("available shard is :");
    for(auto a : m_AvailableShards){
        printf("%d ", a);
    }
    printf("\n");
    printf("----------------------barbages------------------------\n");
    for(auto a : garbages){
        for(auto b : a.second){
            printf("in config%d, the shard%d need gc\n", a.first, b.first);
        }
    }
    printf("-----------------------config--------------------------\n");
    printConfig(this->m_config);
}

void testShardkvSnapShot(){
    unordered_map<string, string> dataBase;
    dataBase["abc"] = "123";
    dataBase["bcd"] = "234";
    dataBase["cde"] = "345";
    unordered_map<int, int> seq;
    seq[1234] = 1;
    seq[6869] = 2;
    seq[9879] = 1;
    unordered_map<int, unordered_map<int, unordered_map<string, string>>> out;
    out[1][2]["abc"] = "123";
    out[1][3]["bcd"] = "234";
    out[2][3]["cde"] = "345";
    unordered_map<int, int> in;
    in[1] = 1;
    in[2] = 1;
    unordered_set<int> shards;
    shards.insert(1);
    shards.insert(2);
    shards.insert(3);
    unordered_map<int, unordered_map<int, bool>> g;
    g[1][2] = true;
    g[1][1] = true;
    g[2][3] = true;
    Config config;
    config.configNum = 3;
    config.shards = {1, 1, 1, 2, 2, 2, 3, 3, 3, 3};
    unordered_map<int, vector<string>> group;
    group[1] = {"11","12", "13", "14", "15"};
    group[2] = {"21","22", "23", "24", "25"};
    config.groups = group;
    ShardKv kv;
    kv.m_database = dataBase;
    kv.m_clientSeqMap = seq;
    kv.toOutShards = out;
    kv.comeInShards = in;
    kv.garbages = g;
    kv.m_AvailableShards = shards;
    kv.m_config = config;
    string str = kv.getSnapShot();
    printf("%s\n", str.c_str());

    ShardKv newKv;
    newKv.recoverySnapShot(str);
    newKv.printSnapShot();
}

int main(){
    testShardkvSnapShot();
}