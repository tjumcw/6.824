#include "raft.hpp"
#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace std;

#define EVERY_SERVER_PORT 3

typedef std::chrono::steady_clock myClock;
typedef std::chrono::steady_clock::time_point myTime;
#define  myDuration std::chrono::duration_cast<std::chrono::microseconds>

class kvServerInfo{
public:
    PeersInfo peersInfo;
    vector<int> m_kvPort;
};

//用于定时的类，创建一个有名管道，若在指定时间内收到msg则处理业务逻辑，不然按照超时处理重试
class Select{
public:
    Select(string fifoName);
    string fifoName;
    bool isRecved;
    static void* work(void* arg);
};

Select::Select(string fifoName){
    this->fifoName = fifoName;
    isRecved = false;
    int ret = mkfifo(fifoName.c_str(), 0664);
    pthread_t test_tid;
    pthread_create(&test_tid, NULL, work, this);
    pthread_detach(test_tid);
}

void* Select::work(void* arg){
    Select* select = (Select*)arg;
    char buf[100];
    int fd = open(select->fifoName.c_str(), O_RDONLY);
    read(fd, buf, sizeof(buf));
    select->isRecved = true;
    close(fd);
    unlink(select->fifoName.c_str());
}

//用于保存处理客户端RPC请求时的上下文信息，每次调用start()且为leader时会存到对应的map中，key为start返回的日志index，独一无二
class OpContext{
public:
    OpContext(Operation op);
    Operation op;
    string fifoName;            //对应当前上下文的有名管道名称
    bool isWrongLeader;
    bool isIgnored;

    //针对get请求
    bool isKeyExisted;
    string value;
};

OpContext::OpContext(Operation op){
    this->op = op;
    fifoName = "fifo-" + to_string(op.clientId) + + "-" + to_string(op.requestId);
    isWrongLeader = false;
    isIgnored = false;
    isKeyExisted = true;
    value = "";
}

class GetArgs{
public:
    string key;
    int clientId;
    int requestId;
    friend Serializer& operator >> (Serializer& in, GetArgs& d) {
		in >> d.key >> d.clientId >> d.requestId;
		return in;
	}
	friend Serializer& operator << (Serializer& out, GetArgs d) {
		out << d.key << d.clientId << d.requestId;
		return out;
	}
};

class GetReply{
public:
    string value;
    bool isWrongLeader;
    bool isKeyExist;
    friend Serializer& operator >> (Serializer& in, GetReply& d) {
		in >> d.value >> d.isWrongLeader >> d.isKeyExist;
		return in;
	}
	friend Serializer& operator << (Serializer& out, GetReply d) {
		out << d.value << d.isWrongLeader << d.isKeyExist;
		return out;
	}
};

class PutAppendArgs{
public:
    string key;
    string value;
    string op;
    int clientId;
    int requestId;
    friend Serializer& operator >> (Serializer& in, PutAppendArgs& d) {
		in >> d.key >> d.value >> d.op >> d.clientId >> d.requestId;
		return in;
	}
	friend Serializer& operator << (Serializer& out, PutAppendArgs d) {
		out << d.key << d.value << d.op << d.clientId << d.requestId;
		return out;
	}
};

class PutAppendReply{
public:
    bool isWrongLeader;
};

class KVServer{
public:
    static void* RPCserver(void* arg);
    static void* applyLoop(void* arg);      //持续监听raft层提交的msg的守护线程
    static void* snapShotLoop(void* arg);   //持续监听raft层日志是否超过给定大小，判断进行快照的守护线程
    void StartKvServer(vector<kvServerInfo>& kvInfo, int me, int maxRaftState);
    vector<PeersInfo> getRaftPort(vector<kvServerInfo>& kvInfo);
    GetReply get(GetArgs args);
    PutAppendReply putAppend(PutAppendArgs args);

    string test(string key){ return m_database[key]; }  //测试其余不是leader的server的状态机

    string getSnapShot();                       //将kvServer的状态信息转化为snapShot
    void recoverySnapShot(string snapShot);     //将从raft层获得的快照安装到kvServer即应用层中(必然已经落后其他的server了，或者是初始化)

    //---------------------------test----------------------------
    bool getRaftState();            //获取raft状态
    void killRaft();                //测试安装快照功能时使用，让raft暂停接受日志
    void activateRaft();            //重新激活raft的功能

private:
    locker m_lock;
    Raft m_raft;
    int m_id;
    vector<int> m_port;
    int cur_portId;

    // bool dead;

    int m_maxraftstate;  //超过这个大小就快照
    int m_lastAppliedIndex;

    unordered_map<string, string> m_database;  //模拟数据库
    unordered_map<int, int> m_clientSeqMap;    //只记录特定客户端已提交的最大请求ID
    unordered_map<int, OpContext*> m_requestMap;  //记录当前RPC对应的上下文
};

void KVServer::StartKvServer(vector<kvServerInfo>& kvInfo, int me, int maxRaftState){
    
    this->m_id = me;
    m_port = kvInfo[me].m_kvPort;
    vector<PeersInfo> peers = getRaftPort(kvInfo);
    this->m_maxraftstate = maxRaftState;
    m_lastAppliedIndex = 0;
    
    m_raft.setRecvSem(1);
    m_raft.setSendSem(0);
    m_raft.Make(peers, me);

    m_database.clear();
    m_clientSeqMap.clear();
    m_requestMap.clear();

    // dead = false;

    pthread_t listen_tid1[m_port.size()];                           //创建多个用于监听客户端请求的RPCserver
    for(int i = 0; i < m_port.size(); i++){
        pthread_create(listen_tid1 + i, NULL, RPCserver, this);
        pthread_detach(listen_tid1[i]);
    }
    pthread_t listen_tid2;
    pthread_create(&listen_tid2, NULL, applyLoop, this);
    pthread_detach(listen_tid2);

    pthread_t listen_tid3;
    pthread_create(&listen_tid3, NULL, snapShotLoop, this);
    pthread_detach(listen_tid3);
}

void* KVServer::RPCserver(void* arg){
    KVServer* kv = (KVServer*) arg;
    buttonrpc server;
    kv->m_lock.lock();
    int port = kv->cur_portId++;
    kv->m_lock.unlock();

    server.as_server(kv->m_port[port]);
    server.bind("get", &KVServer::get, kv);
    server.bind("putAppend", &KVServer::putAppend, kv);
    server.run();
}


//PRChandler for get-request
GetReply KVServer::get(GetArgs args){
    GetReply reply;
    reply.isWrongLeader = false;
    reply.isKeyExist = true;
    Operation operation;
    operation.op = "get";
    operation.key = args.key;
    operation.value = "random";
    operation.clientId = args.clientId;
    operation.requestId = args.requestId;

    StartRet ret = m_raft.start(operation);
    operation.term = ret.m_curTerm;
    operation.index = ret.m_cmdIndex;

    if(ret.isLeader == false){
        printf("client %d's get request is wrong leader %d\n", args.clientId, m_id);
        reply.isWrongLeader = true;
        return reply;
    }

    OpContext opctx(operation);             //创建RPC时的上下文信息并暂存到map中，其key为start返回的该条请求在raft日志中唯一的索引
    m_lock.lock();
    m_requestMap[ret.m_cmdIndex] = &opctx;
    m_lock.unlock();
    Select s(opctx.fifoName);               //创建监听管道数据的定时对象
    myTime curTime = myClock::now();
    while(myDuration(myClock::now() - curTime).count() < 2000000){
        if(s.isRecved){
            // printf("client %d's get->time is %d\n", args.clientId, myDuration(myClock::now() - curTime).count());
            break;
        }
        usleep(10000);
    }

    if(s.isRecved){
        if(opctx.isWrongLeader){
            reply.isWrongLeader = true;
        }else if(!opctx.isKeyExisted){
            reply.isKeyExist = false;
        }else{
            // printf("get hit the key, value is %s\n", opctx.value.c_str());
            reply.value = opctx.value;
            // printf("get hit the key, reply is %s\n", reply.value.c_str());
        }
    }
    else{
        reply.isWrongLeader = true;
        printf("in get --------- timeout!!!\n");
    }
    m_lock.lock();
    m_requestMap.erase(ret.m_cmdIndex);
    m_lock.unlock();
    return reply;
}

//PRChandler for put/append-request
PutAppendReply KVServer::putAppend(PutAppendArgs args){
    PutAppendReply reply;
    reply.isWrongLeader = false;
    Operation operation;
    operation.op = args.op;
    operation.key = args.key;
    operation.value = args.value;
    operation.clientId = args.clientId;
    operation.requestId = args.requestId;

    StartRet ret = m_raft.start(operation);

    operation.term = ret.m_curTerm;
    operation.index = ret.m_cmdIndex;
    if(ret.isLeader == false){
        printf("client %d's putAppend request is wrong leader %d\n", args.clientId, m_id);
        reply.isWrongLeader = true;
        return reply;
    }

    OpContext opctx(operation);             //创建RPC时的上下文信息并暂存到map中，其key为start返回的该条请求在raft日志中唯一的索引
    m_lock.lock();
    m_requestMap[ret.m_cmdIndex] = &opctx;
    m_lock.unlock();

    Select s(opctx.fifoName);               //创建监听管道数据的定时对象
    myTime curTime = myClock::now();
    while(myDuration(myClock::now() - curTime).count() < 2000000){
        if(s.isRecved){
            // printf("client %d's putAppend->time is %d\n", args.clientId, myDuration(myClock::now() - curTime).count());
            break;
        }
        usleep(10000);
    }

    if(s.isRecved){
        // printf("opctx.isWrongLeader : %d\n", opctx.isWrongLeader ? 1 : 0);
        if(opctx.isWrongLeader){
            reply.isWrongLeader = true;
        }else if(opctx.isIgnored){
            //啥也不管即可，请求过期需要被忽略，返回ok让客户端不管即可
        }
    }
    else{
        reply.isWrongLeader = true;
        printf("int putAppend --------- timeout!!!\n");
    }
    m_lock.lock();
    m_requestMap.erase(ret.m_cmdIndex);
    m_lock.unlock();
    return reply;
}

void* KVServer::applyLoop(void* arg){
    KVServer* kv = (KVServer*)arg;
    while(1){

        kv->m_raft.waitSendSem();
        ApplyMsg msg = kv->m_raft.getBackMsg();

        if(!msg.commandValid){                          //为快照处理的逻辑
            kv->m_lock.lock();   
            if(msg.snapShot.size() == 0){
                kv->m_database.clear();
                kv->m_clientSeqMap.clear();
            }else{
                kv->recoverySnapShot(msg.snapShot);
            }
            //一般初始化时安装快照，以及follower收到installSnapShot向上层kvserver发起安装快照请求
            kv->m_lastAppliedIndex = msg.lastIncludedIndex;   
            printf("in stall m_lastAppliedIndex is %d\n", kv->m_lastAppliedIndex);
            kv->m_lock.unlock();
        }else{
            Operation operation = msg.getOperation();
            int index = msg.commandIndex;

            kv->m_lock.lock();
            kv->m_lastAppliedIndex = index;           //收到一个msg就更新m_lastAppliedIndex 
            bool isOpExist = false, isSeqExist = false;
            int prevRequestIdx = INT_MAX;
            OpContext* opctx = NULL;
            if(kv->m_requestMap.count(index)){
                isOpExist = true;
                opctx = kv->m_requestMap[index];
                if(opctx->op.term != operation.term){
                    opctx->isWrongLeader = true;
                    printf("not euqal term -> wrongLeader : opctx %d, op : %d\n", opctx->op.term, operation.term);
                }
            }
            if(kv->m_clientSeqMap.count(operation.clientId)){
                isSeqExist = true;
                prevRequestIdx = kv->m_clientSeqMap[operation.clientId];
            }
            kv->m_clientSeqMap[operation.clientId] = operation.requestId;

            if(operation.op == "put" || operation.op == "append"){
                //非leader的server必然不存在命令，同样处理状态机，leader的第一条命令也不存在，保证按序处理
                if(!isSeqExist || prevRequestIdx < operation.requestId){  
                    if(operation.op == "put"){
                        kv->m_database[operation.key] = operation.value;
                    }else if(operation.op == "append"){
                        if(kv->m_database.count(operation.key)){
                            kv->m_database[operation.key] += operation.value;
                        }else{
                            kv->m_database[operation.key] = operation.value;
                        }
                    }
                }else if(isOpExist){
                    opctx->isIgnored = true;
                }
            }else{
                if(isOpExist){
                    if(kv->m_database.count(operation.key)){
                        opctx->value = kv->m_database[operation.key];       //如果有则返回value
                    }else{
                        opctx->isKeyExisted = false;
                        opctx->value = "";                                  //如果无返回""
                    }
                }
            }

            kv->m_lock.unlock();

            //保证只有存了上下文信息的leader才能唤醒管道，回应clerk的RPC请求(leader需要多做的工作)
            if(isOpExist){  
                int fd = open(opctx->fifoName.c_str(), O_WRONLY);
                char* buf = "12345";
                write(fd, buf, strlen(buf) + 1);
                close(fd);
            }    
        }
        kv->m_raft.postRecvSem();
    }
}

string KVServer::getSnapShot(){
    string snapShot;
    for(const auto& ele : m_database){
        snapShot += ele.first + " " + ele.second + ".";
    }
    snapShot += ";";
    for(const auto& ele : m_clientSeqMap){
        snapShot += to_string(ele.first) + " " + to_string(ele.second) + ".";
    }
    cout<<"int cout snapShot is "<<snapShot<<endl;
    printf("in kvserver -----------------snapShot is %s\n", snapShot.c_str());
    return snapShot;
}


void* KVServer::snapShotLoop(void* arg){
    KVServer* kv = (KVServer*)arg;
    while(1){
        string snapShot = "";
        int lastIncluedIndex;
        // printf("%d not in loop -> kv->m_lastAppliedIndex : %d\n", kv->m_id, kv->m_lastAppliedIndex);
        if(kv->m_maxraftstate != -1 && kv->m_raft.ExceedLogSize(kv->m_maxraftstate)){           //设定了大小且超出大小则应用层进行快照
            kv->m_lock.lock();
            snapShot = kv->getSnapShot();
            lastIncluedIndex = kv->m_lastAppliedIndex;
            // printf("%d in loop -> kv->m_lastAppliedIndex : %d\n", kv->m_id, kv->m_lastAppliedIndex);
            kv->m_lock.unlock();
        }
        if(snapShot.size() != 0){
            kv->m_raft.recvSnapShot(snapShot, lastIncluedIndex);        //向raft层发送快照用于日志压缩，同时持久化
            printf("%d called recvsnapShot size is %d, lastapply is %d\n", kv->m_id, snapShot.size(), kv->m_lastAppliedIndex);
        }
        usleep(10000);
    }
}

vector<kvServerInfo> getKvServerPort(int num){
    vector<kvServerInfo> peers(num);
    for(int i = 0; i < num; i++){
        peers[i].peersInfo.m_peerId = i;
        peers[i].peersInfo.m_port.first = COMMOM_PORT + i;
        peers[i].peersInfo.m_port.second = COMMOM_PORT + i + num;
        peers[i].peersInfo.isInstallFlag = false;
        for(int j = 0; j < EVERY_SERVER_PORT; j++){
            peers[i].m_kvPort.push_back(COMMOM_PORT + i + (j + 2) * num);
        }
        // printf(" id : %d port1 : %d, port2 : %d\n", peers[i].m_peerId, peers[i].m_port.first, peers[i].m_port.second);
    }
    return peers;
}

vector<PeersInfo> KVServer::getRaftPort(vector<kvServerInfo>& kvInfo){
    int n = kvInfo.size();
    vector<PeersInfo> ret(n);
    for(int i = 0; i < n; i++){
        ret[i] = kvInfo[i].peersInfo;
    }
    return ret;
}

void KVServer::recoverySnapShot(string snapShot){
    printf("recovery is called\n");
    vector<string> str;
    string tmp = "";
    for(int i = 0; i < snapShot.size(); i++){
        if(snapShot[i] != ';'){
            tmp += snapShot[i];
        }else{
            if(tmp.size() != 0){
                str.push_back(tmp);
                tmp = "";
            }
        }
    }
    if(tmp.size() != 0) str.push_back(tmp);
    tmp = "";
    vector<string> kvData, clientSeq;
    for(int i = 0; i < str[0].size(); i++){
        if(str[0][i] != '.'){
            tmp += str[0][i];
        }else{
            if(tmp.size() != 0){
                kvData.push_back(tmp);
                tmp = "";
            }
        }
    }
    for(int i = 0; i < str[1].size(); i++){
        if(str[1][i] != '.'){
            tmp += str[1][i];
        }else{
            if(tmp.size() != 0){
                clientSeq.push_back(tmp);
                tmp = "";
            }
        }
    }
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
    for(int i = 0; i < clientSeq.size(); i++){
        tmp = "";
        int j = 0;
        for(; j < clientSeq[i].size(); j++){
            if(clientSeq[i][j] != ' '){
                tmp += clientSeq[i][j];
            }else break;
        }
        string value(clientSeq[i].begin() + j + 1, clientSeq[i].end());
        m_clientSeqMap[atoi(tmp.c_str())] = atoi(value.c_str());
    }
    printf("-----------------databegin---------------------------\n");
    for(auto a : m_database){
        printf("data-> key is %s, value is %s\n", a.first.c_str(), a.second.c_str());
    }
    printf("-----------------requSeqbegin---------------------------\n");
    for(auto a : m_clientSeqMap){
        printf("data-> key is %d, value is %d\n", a.first, a.second);
    }
}

bool KVServer::getRaftState(){
    return m_raft.getState().second;
}

void KVServer::killRaft(){
    m_raft.kill();
}

void KVServer::activateRaft(){
    m_raft.activate();
}

int main(){
    vector<kvServerInfo> servers = getKvServerPort(5);
    srand((unsigned)time(NULL));
    KVServer* kv = new KVServer[servers.size()];
    for(int i = 0; i < 5; i++){
        kv[i].StartKvServer(servers, i, 1024);
    }

    //--------------------------------------test---------------------------------------------
    sleep(3);
    for(int i = 0; i < 5; i++){
        printf("server%d's key : abc -> value is %s\n", i, kv[i].test("abc").c_str());
    }
    sleep(5);
    int i = 2;
    while(1){
        i = rand() % 5;
        if(!kv[i].getRaftState()){
            kv[i].killRaft();          //先让某个不是leader的raft宕机，不接受leader的appendEntriesRPC，让日志落后于leader的快照状态
            break;
        }
    }
    sleep(3);
    kv[i].activateRaft();              //重新激活对应的raft，在raft层发起installRPC请求，且向对应落后的kvServer安装从raft的leader处获得的快照
    //--------------------------------------test---------------------------------------------
    while(1);
}
