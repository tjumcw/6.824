#include "raft.hpp"
#include <bits/stdc++.h>
using namespace std;

// class ServerInfo{
// public:
//     int port;
//     int id;
// };

#define EVERY_SERVER_PORT 3

int cur_portId = 0;
locker port_lock;

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

class Clerk{
public:
    Clerk(vector<vector<int>>& servers);
    string get(string key);
    void put(string key, string value);
    void append(string key, string value);
    void putAppend(string key, string value, string op);
    int getCurRequestId();
    int getCurLeader();
    int getChangeLeader();

private:
    locker m_requestId_lock;
    vector<vector<int>> servers;
    int leaderId;
    int clientId;
    int requestId;
};

Clerk::Clerk(vector<vector<int>>& servers){
    this->servers = servers;
    this->clientId = rand() % 10000 + 1;
    printf("clientId is %d\n", clientId);
    this->requestId = 0;
    this->leaderId = rand() % servers.size();
}

string Clerk::get(string key){
    GetArgs args;
    args.key = key;
    args.clientId = clientId;
    args.requestId = getCurRequestId();
    int cur_leader = getCurLeader();

    port_lock.lock();
    int curPort = (cur_portId++) % EVERY_SERVER_PORT;
    port_lock.unlock();

    while(1){
        buttonrpc client;
        client.as_client("127.0.0.1", servers[cur_leader][curPort]);
        GetReply reply = client.call<GetReply>("get", args).val();
        if(reply.isWrongLeader){
            cur_leader = getChangeLeader();
            usleep(1000);
        }else{
            if(reply.isKeyExist){
                return reply.value;
            }else{
                return "";
            }
        } 
    }   
}

int Clerk::getCurRequestId(){        //封装成原子操作，避免每次加解锁，代码复用
    m_requestId_lock.lock();
    int cur_requestId = requestId++;
    m_requestId_lock.unlock();
    return cur_requestId;
}

int Clerk::getCurLeader(){
    m_requestId_lock.lock();
    int cur_leader = leaderId;
    m_requestId_lock.unlock();
    return cur_leader;
}

int Clerk::getChangeLeader(){
    m_requestId_lock.lock();
    leaderId = (leaderId + 1) % servers.size();
    int new_leader  = leaderId;
    m_requestId_lock.unlock();
    return new_leader;
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
    args.requestId = getCurRequestId();
    int cur_leader = getCurLeader();

    port_lock.lock();
    int curPort = (cur_portId++) % EVERY_SERVER_PORT;
    port_lock.unlock();

    while(1){
        buttonrpc client;
        client.as_client("127.0.0.1", servers[cur_leader][curPort]);
        PutAppendReply reply = client.call<PutAppendReply>("putAppend", args).val();
        if(!reply.isWrongLeader){
            return;
        }
        printf("clerk%d's leader %d is wrong\n", clientId, cur_leader);
        cur_leader = getChangeLeader();
        usleep(1000);
    }   
}

vector<vector<int>> getServerPort(int num){
    vector<vector<int>> kvServerPort(num);
    for(int i = 0; i < num; i++){
        for(int j = 0; j < 3; j++){
            kvServerPort[i].push_back(COMMOM_PORT + i + (j + 2) * num);
        }
    }
    return kvServerPort;
}

int main(){
    srand((unsigned)time(NULL));
    vector<vector<int>> port = getServerPort(5);
    // printf("server.size() = %d\n", port.size());
    Clerk clerk(port);
    Clerk clerk2(port);
    Clerk clerk3(port);
    Clerk clerk4(port);
    Clerk clerk5(port);

    while(1){
        clerk.put("abc", "123");
        cout << clerk.get("abc") << endl;
        clerk2.put("abc", "456");
        clerk3.append("abc", "789");
        cout << clerk.get("abc") << endl;
        clerk4.put("bcd", "111");
        cout << clerk.get("bcd") << endl;
        clerk5.append("bcd", "222");
        cout << clerk3.get("bcd") << endl;
        cout << clerk3.get("abcd") << endl;
        clerk5.append("bcd", "222");
        clerk4.append("bcd", "222");
        cout << clerk2.get("bcd") << endl;
        clerk3.append("bcd", "222");
        clerk2.append("bcd", "232");
        cout << clerk4.get("bcd") << endl;
        clerk.append("bcd", "222");
        clerk4.put("bcd", "111");
        cout << clerk3.get("bcd") << endl;
        usleep(10000);
    }
}
