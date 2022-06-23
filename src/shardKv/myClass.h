#ifndef __MYCLASS__H
#define __MYCLASS__H

#include "./shardmaster/shardClerk.hpp"
#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace std;

typedef std::chrono::steady_clock myClock;
typedef std::chrono::steady_clock::time_point myTime;
#define  myDuration std::chrono::duration_cast<std::chrono::microseconds>

#define OK "OK"
#define ErrNoKey  "ErrNoKey"
#define ErrWrongGroup "ErrWrongGroup"
#define ErrWrongLeader "ErrWrongLeader"

class kvServerInfo{
public:
    PeersInfo peersInfo;
    int m_kvPort;
};

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

class OpContext{
public:
    OpContext(Operation op);
    Operation op;
    string fifoName;
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
    string err;
    friend Serializer& operator >> (Serializer& in, GetReply& d) {
		in >> d.value >> d.err;
		return in;
	}
	friend Serializer& operator << (Serializer& out, GetReply d) {
		out << d.value << d.err;
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
    string err;
    friend Serializer& operator >> (Serializer& in, PutAppendReply& d) {
		in >> d.err;
		return in;
	}
	friend Serializer& operator << (Serializer& out, PutAppendReply d) {
		out << d.err;
		return out;
	}
};

class MigrateArgs{
public:
    int shard;
    int configNum;
};

class MigrateReply{
public:
    string err;
    int shard;
    int configNum;
    unordered_map<string, string> database;
    unordered_map<int, int> clientReqId;
};

class MigrateRpcReply{
public:
    string reply;
    friend Serializer& operator >> (Serializer& in, MigrateRpcReply& d) {
		in >> d.reply;
		return in;
	}
	friend Serializer& operator << (Serializer& out, MigrateRpcReply d) {
		out << d.reply;
		return out;
	}
};

class GarbagesCollectArgs{
public:
    int shard;
    int configNum;
};

class GarbagesCollectReply{
public:
    string err;
    friend Serializer& operator >> (Serializer& in, GarbagesCollectReply& d) {
		in >> d.err;
		return in;
	}
	friend Serializer& operator << (Serializer& out, GarbagesCollectReply d) {
		out << d.err;
		return out;
	}
};

#endif