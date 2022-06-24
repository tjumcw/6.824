#ifndef __RAFT__H
#define __RAFT__H

#include <iostream>
#include <bits/stdc++.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include "locker.h"
#include "./buttonrpc-master/buttonrpc.hpp"
#include "common.h"
using namespace std;

#define COMMOM_PORT 1234
#define HEART_BEART_PERIOD 100000

/**
 * @brief 看LAB2,3的raft注释，基本都一样，只是在LAB3的基础上小小修改了operation及applyMsg的内容
 * 
 */

class InstallSnapShotArgs{
public:
    int term;
    int leaderId;
    int lastIncludedIndex;
    int lastIncludedTerm;
    string snapShot;

    friend Serializer& operator >> (Serializer& in, InstallSnapShotArgs& d) {
		in >> d.term >> d.leaderId >> d.lastIncludedIndex >> d.lastIncludedTerm >> d.snapShot;
		return in;
	}
	friend Serializer& operator << (Serializer& out, InstallSnapShotArgs d) {
		out << d.term << d.leaderId << d.lastIncludedIndex << d.lastIncludedTerm << d.snapShot;
		return out;
	}
};

class InstallSnapSHotReply{
public:
    int term;
};

class Operation{
public:
    string getCmd();
    string getArgs(string op);
    string op;
    string args;
    string key;
    string value;
    int clientId;
    int requestId;
    int term;
    int index;
};

string Operation::getCmd(){
    string cmd = op + " " + key + " " + value + " " + to_string(clientId) + " " + to_string(requestId) + " " + args;
    return cmd;
}

class StartRet{
public:
    StartRet():m_cmdIndex(-1), m_curTerm(-1), isLeader(false){}
    int m_cmdIndex;
    int m_curTerm;
    bool isLeader;
};

class ApplyMsg {
public:
    bool commandValid;
	string command;
    int commandIndex;
    int commandTerm;
    Operation getOperation();

    int lastIncludedIndex;
    int lastIncludedTerm;
    string snapShot;
};

Operation ApplyMsg::getOperation(){
    Operation operation;
    vector<string> str;
    string tmp;
    for(int i = 0; i < command.size(); i++){
        if(command[i] != ' '){
            tmp += command[i];
        }else{
            if(tmp.size() != 0) str.push_back(tmp);
            tmp = "";
        }
    }
    if(tmp.size() != 0){
        str.push_back(tmp);
    }
    operation.op = str[0];
    operation.key = str[1];
    operation.value = str[2];
    operation.clientId = atoi(str[3].c_str());
    operation.requestId = atoi(str[4].c_str());
    operation.args = str[5];
    operation.term = commandTerm;
    return operation;
}

class PeersInfo{
public:
    pair<int, int> m_port;
    int m_peerId;
    bool isInstallFlag;
};

class LogEntry{
public:
    LogEntry(string cmd = "", int term = -1):m_command(cmd),m_term(term){}
    string m_command;
    int m_term;
};

class Persister{
public:
    vector<LogEntry> logs;
    string snapShot;
    int cur_term;
    int votedFor;
    int lastIncludedIndex;
    int lastIncludedTerm;
};

class AppendEntriesArgs{
public:
    // AppendEntriesArgs():m_term(-1), m_leaderId(-1), m_prevLogIndex(-1), m_prevLogTerm(-1){
    //     //m_leaderCommit = 0;
    //     m_sendLogs.clear();
    // }
    int m_term;
    int m_leaderId;
    int m_prevLogIndex;
    int m_prevLogTerm;
    int m_leaderCommit;
    string m_sendLogs;
    friend Serializer& operator >> (Serializer& in, AppendEntriesArgs& d) {
		in >> d.m_term >> d.m_leaderId >> d.m_prevLogIndex >> d.m_prevLogTerm >> d.m_leaderCommit >> d.m_sendLogs;
		return in;
	}
	friend Serializer& operator << (Serializer& out, AppendEntriesArgs d) {
		out << d.m_term << d.m_leaderId << d.m_prevLogIndex << d.m_prevLogTerm << d.m_leaderCommit << d.m_sendLogs;
		return out;
	}
};

class AppendEntriesReply{
public:
    int m_term;
    bool m_success;
    int m_conflict_term;
    int m_conflict_index;
};

class RequestVoteArgs{
public:
    int term;
    int candidateId;
    int lastLogTerm;
    int lastLogIndex;
};

class RequestVoteReply{
public:
    int term;
    bool VoteGranted;
};

class Raft{
public:
    static void* listenForVote(void* arg);
    static void* listenForAppend(void* arg);
    static void* processEntriesLoop(void* arg);
    static void* electionLoop(void* arg);
    static void* callRequestVote(void* arg);
    static void* sendAppendEntries(void* arg);
    static void* sendInstallSnapShot(void* arg);
    static void* applyLogLoop(void* arg);

    enum RAFT_STATE {LEADER = 0, CANDIDATE, FOLLOWER};
    void Make(vector<PeersInfo> peers, int id);
    int getMyduration(timeval last);
    void setBroadcastTime();
    pair<int, bool> getState();
    RequestVoteReply requestVote(RequestVoteArgs args);   
    AppendEntriesReply appendEntries(AppendEntriesArgs args);
    InstallSnapSHotReply installSnapShot(InstallSnapShotArgs args);
    bool checkLogUptodate(int term, int index);
    void push_backLog(LogEntry log);
    vector<LogEntry> getCmdAndTerm(string text);
    StartRet start(Operation op);
    void printLogs();
    void setSendSem(int num);
    void setRecvSem(int num);
    bool waitSendSem();
    bool waitRecvSem();
    bool postSendSem();
    bool postRecvSem();
    ApplyMsg getBackMsg();

    void serialize();
    bool deserialize();
    void saveRaftState();
    void readRaftState();
    bool isKilled();  //->check is killed?
    void kill();  
    void activate();

    bool ExceedLogSize(int size);
    void recvSnapShot(string snapShot, int lastIncludedIndex);
    int idxToCompressLogPos(int index);
    bool readSnapShot();
    void saveSnapShot();
    void installSnapShotTokvServer();
    int lastIndex();
    int lastTerm();

private:
    locker m_lock;
    cond m_cond;
    vector<PeersInfo> m_peers;
    Persister persister;
    int m_peerId;
    int dead;

    //需要持久化的data
    int m_curTerm;
    int m_votedFor;
    vector<LogEntry> m_logs;
    int m_lastIncludedIndex;
    int m_lastIncludedTerm;

    vector<int> m_nextIndex;
    vector<int> m_matchIndex;
    int m_lastApplied;
    int m_commitIndex;

    // unordered_map<int, int> m_firstIndexOfEachTerm;
    // vector<int> m_nextIndex;
    // vector<int> m_matchIndex;

    int recvVotes;
    int finishedVote;
    int cur_peerId;

    RAFT_STATE m_state;
    int m_leaderId;
    struct timeval m_lastWakeTime;
    struct timeval m_lastBroadcastTime;

    //用作与kvRaft交互
    sem m_recvSem;
    sem m_sendSem;
    vector<ApplyMsg> m_msgs;

    bool installSnapShotFlag;
    bool applyLogFlag;
    unordered_set<int> isExistIndex;
};

void Raft::Make(vector<PeersInfo> peers, int id){
    m_peers = peers;
    //this->persister = persister;
    m_peerId = id;
    dead = 0;

    m_state = FOLLOWER;
    m_curTerm = 0;
    m_leaderId = -1;
    m_votedFor = -1;
    gettimeofday(&m_lastWakeTime, NULL);
    // readPersist(persister.ReadRaftState());

    // for(int i = 0; i < id + 1; i++){
    //     LogEntry log;
    //     log.m_command = to_string(i);
    //     log.m_term = i;
    //     m_logs.push_back(log);
    // }

    recvVotes = 0;
    finishedVote = 0;
    cur_peerId = 0;

    m_lastApplied = 0;
    m_commitIndex = 0;
    m_nextIndex.resize(peers.size(), 1);
    m_matchIndex.resize(peers.size(), 0);

    m_lastIncludedIndex = 0;
    m_lastIncludedTerm = 0;
    isExistIndex.clear();

    readRaftState();
    installSnapShotTokvServer();
    
    pthread_t listen_tid1;
    pthread_t listen_tid2;
    pthread_t listen_tid3;
    pthread_create(&listen_tid1, NULL, listenForVote, this);
    pthread_detach(listen_tid1);
    pthread_create(&listen_tid2, NULL, listenForAppend, this);
    pthread_detach(listen_tid2);
    pthread_create(&listen_tid3, NULL, applyLogLoop, this);
    pthread_detach(listen_tid3);
}

void* Raft::applyLogLoop(void* arg){
    Raft* raft = (Raft*)arg;
    while(1){
        while(!raft->dead){
            // raft->m_lock.lock();
            // if(raft->installSnapShotFlag){
            //     printf("%d check install : %d, apply : %d\n", raft->m_peerId, 
            //         raft->installSnapShotFlag? 1 : 0, raft->applyLogFlag ? 1 : 0);
            //     raft->applyLogFlag = false;
            //     raft->m_lock.unlock();
            //     usleep(10000);
            //     continue;
            // }
            // raft->m_lock.unlock();
            usleep(10000);
            // printf("%d's apply is called, apply is %d, commit is %d\n", raft->m_peerId, raft->m_lastApplied, raft->m_commitIndex);
            vector<ApplyMsg> msgs;
            raft->m_lock.lock();
            while(raft->m_lastApplied < raft->m_commitIndex){
                raft->m_lastApplied++;
                int appliedIdx = raft->idxToCompressLogPos(raft->m_lastApplied);
                ApplyMsg msg;
                msg.command = raft->m_logs[appliedIdx].m_command;
                msg.commandValid = true;
                msg.commandTerm = raft->m_logs[appliedIdx].m_term;
                msg.commandIndex = raft->m_lastApplied;
                msgs.push_back(msg);
            }
            raft->m_lock.unlock();
            for(int i = 0; i < msgs.size(); i++){
                // printf("before %d's apply is called, apply is %d, commit is %d\n", 
                //     raft->m_peerId, raft->m_lastApplied, raft->m_commitIndex);
                raft->waitRecvSem();
                // printf("after %d's apply is called, apply is %d, commit is %d\n", 
                //     raft->m_peerId, raft->m_lastApplied, raft->m_commitIndex);
                raft->m_msgs.push_back(msgs[i]);
                raft->postSendSem();
            }
        }
        usleep(10000);
    }
}

int Raft::getMyduration(timeval last){
    struct timeval now;
    gettimeofday(&now, NULL);
    // printf("--------------------------------\n");
    // printf("now's sec : %ld, now's usec : %ld\n", now.tv_sec, now.tv_usec);
    // printf("last's sec : %ld, last's usec : %ld\n", last.tv_sec, last.tv_usec);
    // printf("%d\n", ((now.tv_sec - last.tv_sec) * 1000000 + (now.tv_usec - last.tv_usec)));
    // printf("--------------------------------\n");
    return ((now.tv_sec - last.tv_sec) * 1000000 + (now.tv_usec - last.tv_usec));
}   

void Raft::setBroadcastTime(){
    gettimeofday(&m_lastBroadcastTime, NULL);
    printf("before : %ld, %ld\n", m_lastBroadcastTime.tv_sec, m_lastBroadcastTime.tv_usec);
    if(m_lastBroadcastTime.tv_usec >= 200000){
        m_lastBroadcastTime.tv_usec -= 200000;
    }else{
        m_lastBroadcastTime.tv_sec -= 1;
        m_lastBroadcastTime.tv_usec += (1000000 - 200000);
    }
}

void* Raft::listenForVote(void* arg){
    Raft* raft = (Raft*)arg;
    buttonrpc server;
    server.as_server(raft->m_peers[raft->m_peerId].m_port.first);
    server.bind("requestVote", &Raft::requestVote, raft);

    pthread_t wait_tid;
    pthread_create(&wait_tid, NULL, electionLoop, raft);
    pthread_detach(wait_tid);

    server.run();
    printf("exit!\n");
}

void* Raft::listenForAppend(void* arg){
    Raft* raft = (Raft*)arg;
    buttonrpc server;
    server.as_server(raft->m_peers[raft->m_peerId].m_port.second);
    server.bind("appendEntries", &Raft::appendEntries, raft);
    server.bind("installSnapShot", &Raft::installSnapShot, raft);
    pthread_t heart_tid;
    pthread_create(&heart_tid, NULL, processEntriesLoop, raft);
    pthread_detach(heart_tid);

    server.run();
    printf("exit!\n");
}

void* Raft::electionLoop(void* arg){
    Raft* raft = (Raft*)arg;
    bool resetFlag = false;
    while(!raft->dead){
        
        int timeOut = rand()%200000 + 200000;
        while(1){
            usleep(1000);
            raft->m_lock.lock();

            int during_time = raft->getMyduration(raft->m_lastWakeTime);
            if(raft->m_state == FOLLOWER && during_time > timeOut){
                raft->m_state = CANDIDATE;
            }

            if(raft->m_state == CANDIDATE && during_time > timeOut){
                printf(" %d attempt election at term %d, timeOut is %d\n", raft->m_peerId, raft->m_curTerm, timeOut);
                gettimeofday(&raft->m_lastWakeTime, NULL);
                resetFlag = true;
                raft->m_curTerm++;
                raft->m_votedFor = raft->m_peerId;
                raft->saveRaftState();

                raft->recvVotes = 1;
                raft->finishedVote = 1;
                raft->cur_peerId = 0;
                
                pthread_t tid[raft->m_peers.size() - 1];
                int i = 0;
                for(auto server : raft->m_peers){
                    if(server.m_peerId == raft->m_peerId) continue;
                    pthread_create(tid + i, NULL, callRequestVote, raft);
                    pthread_detach(tid[i]);
                    i++;
                }

                while(raft->recvVotes <= raft->m_peers.size() / 2 && raft->finishedVote != raft->m_peers.size()){
                    raft->m_cond.wait(raft->m_lock.getLock());
                }
                if(raft->m_state != CANDIDATE){
                    raft->m_lock.unlock();
                    continue;
                }
                if(raft->recvVotes > raft->m_peers.size() / 2){
                    raft->m_state = LEADER;

                    for(int i = 0; i < raft->m_peers.size(); i++){
                        raft->m_nextIndex[i] = raft->lastIndex() + 1;
                        raft->m_matchIndex[i] = 0;
                    }

                    printf(" %d become new leader at term %d\n", raft->m_peerId, raft->m_curTerm);
                    raft->setBroadcastTime();
                }
            }
            raft->m_lock.unlock();
            if(resetFlag){
                resetFlag = false;
                break;
            }
        }
        
    }
}

void* Raft::callRequestVote(void* arg){
    Raft* raft = (Raft*) arg;
    buttonrpc client;
    raft->m_lock.lock();
    RequestVoteArgs args;
    args.candidateId = raft->m_peerId;
    args.term = raft->m_curTerm;
    args.lastLogIndex = raft->lastIndex();
    args.lastLogTerm = raft->lastTerm();
    if(raft->cur_peerId == raft->m_peerId){
        raft->cur_peerId++;     
    }
    int clientPeerId = raft->cur_peerId;
    client.as_client("127.0.0.1", raft->m_peers[raft->cur_peerId++].m_port.first);

    if(raft->cur_peerId == raft->m_peers.size() || 
            (raft->cur_peerId == raft->m_peers.size() - 1 && raft->m_peerId == raft->cur_peerId)){
        raft->cur_peerId = 0;
    }
    raft->m_lock.unlock();

    RequestVoteReply reply = client.call<RequestVoteReply>("requestVote", args).val();
    raft->m_lock.lock();
    raft->finishedVote++;
    raft->m_cond.signal();
    if(reply.term > raft->m_curTerm){
        raft->m_state = FOLLOWER;
        raft->m_curTerm = reply.term;
        raft->m_votedFor = -1;
        raft->readRaftState();
        raft->m_lock.unlock();
        return NULL;
    }
    if(reply.VoteGranted){
        raft->recvVotes++;
    }
    raft->m_lock.unlock();
}

bool Raft::checkLogUptodate(int term, int index){
    int lastTerm = this->lastTerm();
    if(term > lastTerm){
        return true;
    }
    if(term == lastTerm && index >= lastIndex()){
        return true;
    }
    return false;
}

RequestVoteReply Raft::requestVote(RequestVoteArgs args){
    RequestVoteReply reply;
    reply.VoteGranted = false;
    m_lock.lock();
    reply.term = m_curTerm;

    if(m_curTerm > args.term){
        m_lock.unlock();
        return reply;
    }

    if(m_curTerm < args.term){
        m_state = FOLLOWER;
        m_curTerm = args.term;
        m_votedFor = -1;
    }

    if(m_votedFor == -1 || m_votedFor == args.candidateId){
        bool ret = checkLogUptodate(args.lastLogTerm, args.lastLogIndex);
        if(!ret){
            m_lock.unlock();
            return reply;
        }
        m_votedFor = args.candidateId;
        reply.VoteGranted = true;
        printf("[%d] vote to [%d] at %d, duration is %d\n", m_peerId, args.candidateId, m_curTerm, getMyduration(m_lastWakeTime));
        gettimeofday(&m_lastWakeTime, NULL);
    }
    saveRaftState();
    m_lock.unlock();
    return reply;
}

void* Raft::processEntriesLoop(void* arg){
    Raft* raft = (Raft*)arg;
    while(!raft->dead){
        usleep(1000);
        raft->m_lock.lock();
        if(raft->m_state != LEADER){
            raft->m_lock.unlock();
            continue;
        }
        // printf("sec : %ld, usec : %ld\n", raft->m_lastBroadcastTime.tv_sec, raft->m_lastBroadcastTime.tv_usec);
        int during_time = raft->getMyduration(raft->m_lastBroadcastTime);
        // printf("time is %d\n", during_time);
        if(during_time < HEART_BEART_PERIOD){
            raft->m_lock.unlock();
            continue;
        }

        gettimeofday(&raft->m_lastBroadcastTime, NULL);
        // printf("%d send AppendRetries at %d\n", raft->m_peerId, raft->m_curTerm);
        // raft->m_lock.unlock();

        pthread_t tid[raft->m_peers.size() - 1];
        int i = 0;
        for(auto& server : raft->m_peers){
            if(server.m_peerId == raft->m_peerId) continue;
            if(raft->m_nextIndex[server.m_peerId] <= raft->m_lastIncludedIndex){
                printf("%d send install rpc to %d, whose nextIdx is %d, but leader's lastincludeIdx is %d\n", 
                    raft->m_peerId, server.m_peerId, raft->m_nextIndex[server.m_peerId], raft->m_lastIncludedIndex);
                server.isInstallFlag = true;
                pthread_create(tid + i, NULL, sendInstallSnapShot, raft);
                pthread_detach(tid[i]);
            }else{
                // printf("%d send append rpc to %d, whose nextIdx is %d, but leader's lastincludeIdx is %d\n", 
                //     raft->m_peerId, server.m_peerId, raft->m_nextIndex[server.m_peerId], raft->m_lastIncludedIndex);
                pthread_create(tid + i, NULL, sendAppendEntries, raft);
                pthread_detach(tid[i]);
            }
            i++;
        }
        raft->m_lock.unlock();
    }
}

void* Raft::sendInstallSnapShot(void* arg){
    Raft* raft = (Raft*)arg;
    buttonrpc client;
    InstallSnapShotArgs args;
    int clientPeerId;
    raft->m_lock.lock();
    for(int i = 0; i < raft->m_peers.size(); i++){
        // printf("in install %d's server.isInstallFlag is %d\n", i, raft->m_peers[i].isInstallFlag ? 1 : 0);
    }
    for(int i = 0; i < raft->m_peers.size(); i++){
        if(raft->m_peers[i].m_peerId == raft->m_peerId){
            // printf("%d is leader, continue\n", i);
            continue;
        }
        if(!raft->m_peers[i].isInstallFlag){
            // printf("%d is append, continue\n", i);
            continue;
        }
        if(raft->isExistIndex.count(i)){
            // printf("%d is chongfu, continue\n", i);
            continue;
        }
        clientPeerId = i;
        raft->isExistIndex.insert(i);
        // printf("%d in install insert index : %d, size is %d\n", raft->m_peerId, i, raft->isExistIndex.size());
        break;
    }

    client.as_client("127.0.0.1", raft->m_peers[clientPeerId].m_port.second);
    

    if(raft->isExistIndex.size() == raft->m_peers.size() - 1){
        // printf("install clear size is %d\n", raft->isExistIndex.size());
        for(int i = 0; i < raft->m_peers.size(); i++){
            raft->m_peers[i].isInstallFlag = false;
        }
        raft->isExistIndex.clear();
    }

    args.lastIncludedIndex = raft->m_lastIncludedIndex;
    args.lastIncludedTerm = raft->m_lastIncludedTerm;
    args.leaderId = raft->m_peerId;
    args.term = raft->m_curTerm;
    raft->readSnapShot();       
    args.snapShot = raft->persister.snapShot;

    printf("in send install snapShot is %s\n", args.snapShot.c_str());

    raft->m_lock.unlock();
    // printf("%d send to %d's install port is %d\n", raft->m_peerId, clientPeerId, raft->m_peers[clientPeerId].m_port.second);
    InstallSnapSHotReply reply = client.call<InstallSnapSHotReply>("installSnapShot", args).val();
    // printf("%d is called send install to %d\n", raft->m_peerId, clientPeerId);

    raft->m_lock.lock();
    if(raft->m_curTerm != args.term){
        raft->m_lock.unlock();
        return NULL;
    }

    if(raft->m_curTerm < reply.term){
        raft->m_state = FOLLOWER;
        raft->m_votedFor = -1;
        raft->m_curTerm = reply.term;
        raft->saveRaftState();
        raft->m_lock.unlock();
        return NULL;    
    }

    raft->m_nextIndex[clientPeerId] = raft->lastIndex() + 1;
    raft->m_matchIndex[clientPeerId] = args.lastIncludedIndex;

    raft->m_matchIndex[raft->m_peerId] = raft->lastIndex();
    vector<int> tmpIndex = raft->m_matchIndex;
    sort(tmpIndex.begin(), tmpIndex.end());
    int realMajorityMatchIndex = tmpIndex[tmpIndex.size() / 2];
    if(realMajorityMatchIndex > raft->m_commitIndex && (realMajorityMatchIndex <= raft->m_lastIncludedIndex || raft->m_logs[raft->idxToCompressLogPos(realMajorityMatchIndex)].m_term == raft->m_curTerm)){
        raft->m_commitIndex = realMajorityMatchIndex;
    }
    raft->m_lock.unlock();
    
}

InstallSnapSHotReply Raft::installSnapShot(InstallSnapShotArgs args){
    InstallSnapSHotReply reply;
    m_lock.lock();
    reply.term = m_curTerm;
    
    if(args.term < m_curTerm){
        m_lock.unlock();
        return reply;
    }

    if(args.term >= m_curTerm){
        if(args.term > m_curTerm){
            m_votedFor = -1;
            saveRaftState();
        }
        m_curTerm = args.term;
        m_state = FOLLOWER;

    }
    gettimeofday(&m_lastWakeTime, NULL);

    printf("in stall rpc, args.last is %d, but selfLast is %d, size is %d\n", 
        args.lastIncludedIndex, m_lastIncludedIndex, lastIndex());
    if(args.lastIncludedIndex <= m_lastIncludedIndex){
        m_lock.unlock();
        return reply;
    }else{
        if(args.lastIncludedIndex < lastIndex()){
            if(m_logs[idxToCompressLogPos(lastIndex())].m_term != args.lastIncludedTerm){
                m_logs.clear();
            }else{
                vector<LogEntry> tmpLog(m_logs.begin() + idxToCompressLogPos(args.lastIncludedIndex) + 1, m_logs.end());
                m_logs = tmpLog;
            }
        }else{
            m_logs.clear();
        }
    }

    m_lastIncludedIndex = args.lastIncludedIndex;
    m_lastIncludedTerm = args.lastIncludedTerm;
    persister.snapShot = args.snapShot;
    printf("in raft stall rpc, snapShot is %s\n", persister.snapShot.c_str());
    saveRaftState();
    saveSnapShot();

    m_lock.unlock();
    installSnapShotTokvServer();
    return reply;
}

vector<LogEntry> Raft::getCmdAndTerm(string text){
    vector<LogEntry> logs;
    int n = text.size();
    vector<string> str;
    string tmp = "";
    for(int i = 0; i < n; i++){
        if(text[i] != ';'){
            tmp += text[i];
        }else{
            if(tmp.size() != 0) str.push_back(tmp);
            tmp = "";
        }
    }
    for(int i = 0; i < str.size(); i++){
        tmp = "";
        int j = 0;
        for(; j < str[i].size(); j++){
            if(str[i][j] != ','){
                tmp += str[i][j];
            }else break;
        }
        string number(str[i].begin() + j + 1, str[i].end());
        int num = atoi(number.c_str());
        logs.push_back(LogEntry(tmp, num));
    }
    return logs;
}

void Raft::push_backLog(LogEntry log){
    m_logs.push_back(log);
}

void* Raft::sendAppendEntries(void* arg){
    Raft* raft = (Raft*)arg;

    buttonrpc client;
    AppendEntriesArgs args;
    int clientPeerId;
    raft->m_lock.lock();

    // for(int i = 0; i < raft->m_peers.size(); i++){
    //     printf("in append %d's server.isInstallFlag is %d\n", i, raft->m_peers[i].isInstallFlag ? 1 : 0);
    // }

    for(int i = 0; i < raft->m_peers.size(); i++){
        if(raft->m_peers[i].m_peerId == raft->m_peerId) continue;
        if(raft->m_peers[i].isInstallFlag) continue;
        if(raft->isExistIndex.count(i)) continue;
        clientPeerId = i;
        raft->isExistIndex.insert(i);
        // printf("%d in append insert index : %d, size is %d\n", raft->m_peerId, i, raft->isExistIndex.size());
        break;
    }

    client.as_client("127.0.0.1", raft->m_peers[clientPeerId].m_port.second);
    // printf("%d send to %d's append port is %d\n", raft->m_peerId, clientPeerId, raft->m_peers[clientPeerId].m_port.second);

    if(raft->isExistIndex.size() == raft->m_peers.size() - 1){
        // printf("append clear size is %d\n", raft->isExistIndex.size());
        for(int i = 0; i < raft->m_peers.size(); i++){
            raft->m_peers[i].isInstallFlag = false;
        }
        raft->isExistIndex.clear();
    }
    
    args.m_term = raft->m_curTerm;
    args.m_leaderId = raft->m_peerId;
    args.m_prevLogIndex = raft->m_nextIndex[clientPeerId] - 1;
    args.m_leaderCommit = raft->m_commitIndex;

    for(int i = raft->idxToCompressLogPos(args.m_prevLogIndex) + 1; i < raft->m_logs.size(); i++){
        args.m_sendLogs += (raft->m_logs[i].m_command + "," + to_string(raft->m_logs[i].m_term) + ";");
    }


    //用作自己调试可能，因为如果leader的m_prevLogIndex为0，follower的size必为0，自己调试直接赋日志给各个server看选举情况可能需要这段代码
    // if(args.m_prevLogIndex == 0){
    //     args.m_prevLogTerm = 0;
    //     if(raft->m_logs.size() != 0){
    //         args.m_prevLogTerm = raft->m_logs[0].m_term;
    //     }
    // }

    if(args.m_prevLogIndex == raft->m_lastIncludedIndex){
        args.m_prevLogTerm = raft->m_lastIncludedTerm;
    }else{    //有快照的话m_prevLogIndex必然不为0
        args.m_prevLogTerm = raft->m_logs[raft->idxToCompressLogPos(args.m_prevLogIndex)].m_term;
    }

    // printf("[%d] -> [%d]'s prevLogIndex : %d, prevLogTerm : %d\n", raft->m_peerId, clientPeerId, args.m_prevLogIndex, args.m_prevLogTerm); 
    
    raft->m_lock.unlock();
    AppendEntriesReply reply = client.call<AppendEntriesReply>("appendEntries", args).val();

    raft->m_lock.lock();
    if(raft->m_curTerm != args.m_term){
        raft->m_lock.unlock();
        return NULL;
    }
    if(reply.m_term > raft->m_curTerm){
        raft->m_state = FOLLOWER;
        raft->m_curTerm = reply.m_term;
        raft->m_votedFor = -1;
        raft->saveRaftState();
        raft->m_lock.unlock();
        return NULL;                        //FOLLOWER没必要维护nextIndex,成为leader会更新
    }

    if(reply.m_success){
        raft->m_nextIndex[clientPeerId] = args.m_prevLogIndex + raft->getCmdAndTerm(args.m_sendLogs).size() + 1;  //可能RPC调用完log又增加了，但那些是不应该算进去的，不能直接取m_logs.size() + 1
        raft->m_matchIndex[clientPeerId] = raft->m_nextIndex[clientPeerId] - 1;
        raft->m_matchIndex[raft->m_peerId] = raft->lastIndex();

        vector<int> tmpIndex = raft->m_matchIndex;
        sort(tmpIndex.begin(), tmpIndex.end());
        int realMajorityMatchIndex = tmpIndex[tmpIndex.size() / 2];
        if(realMajorityMatchIndex > raft->m_commitIndex && (realMajorityMatchIndex <= raft->m_lastIncludedIndex || raft->m_logs[raft->idxToCompressLogPos(realMajorityMatchIndex)].m_term == raft->m_curTerm)){
            raft->m_commitIndex = realMajorityMatchIndex;
        }
    }

    if(!reply.m_success){
        if(reply.m_conflict_term != -1 && reply.m_conflict_term != -100){
            int leader_conflict_index = -1;
            for(int index = args.m_prevLogIndex; index > raft->m_lastIncludedIndex; index--){
                if(raft->m_logs[raft->idxToCompressLogPos(index)].m_term == reply.m_conflict_term){
                    leader_conflict_index = index;
                    break;
                }
            }
            if(leader_conflict_index != -1){
                raft->m_nextIndex[clientPeerId] = leader_conflict_index + 1;
            }else{
                raft->m_nextIndex[clientPeerId] = reply.m_conflict_term; //这里加不加1都可，无非是多一位还是少一位，此处指follower对应index为空
            }
        }else{
            if(reply.m_conflict_term == -100){

            }
            //-------------------很关键，运行时不能注释下面这段，因为我自己调试bug强行增加bug，没有专门的测试程序-----------------
            else raft->m_nextIndex[clientPeerId] = reply.m_conflict_index;
        }
        
    }
    raft->saveRaftState();
    raft->m_lock.unlock();

}

AppendEntriesReply Raft::appendEntries(AppendEntriesArgs args){
    vector<LogEntry> recvLog = getCmdAndTerm(args.m_sendLogs);
    AppendEntriesReply reply;
    m_lock.lock();
    reply.m_term = m_curTerm;
    reply.m_success = false;
    reply.m_conflict_index = -1;
    reply.m_conflict_term = -1;


    if(args.m_term < m_curTerm){
        m_lock.unlock();
        return reply;
    }

    if(args.m_term >= m_curTerm){
        if(args.m_term > m_curTerm){
            m_votedFor = -1;
            saveRaftState();
        }
        m_curTerm = args.m_term;
        m_state = FOLLOWER;

    }
    // printf("[%d] recv append from [%d] at self term%d, send term %d, duration is %d\n",
    //         m_peerId, args.m_leaderId, m_curTerm, args.m_term, getMyduration(m_lastWakeTime));
    gettimeofday(&m_lastWakeTime, NULL);

    
    //------------------------------------test----------------------------------
    if(dead){ 
        reply.m_conflict_term = -100;
        m_lock.unlock();
        return reply;
    }
    //------------------------------------test----------------------------------

    if(args.m_prevLogIndex < m_lastIncludedIndex){
        printf("[%d]'s m_lastIncludedIndex is %d, but args.m_prevLogIndex is %d\n", 
            m_peerId, m_lastIncludedIndex, args.m_prevLogIndex);
        reply.m_conflict_index = 1;
        m_lock.unlock();
        return reply;
    }else if(args.m_prevLogIndex == m_lastIncludedIndex){
        // printf("[%d]'s m_lastIncludedTerm is %d, args.m_prevLogTerm is %d\n", m_peerId, m_lastIncludedTerm, args.m_prevLogTerm);
        if(args.m_prevLogTerm != m_lastIncludedTerm){     //脑裂分区，少数派的snapShot不对，回归集群后需要更新自己的snapShot及log
            reply.m_conflict_index = 1;
            m_lock.unlock();
            return reply;
        }
    }else{
        if(lastIndex() < args.m_prevLogIndex){        
            //索引要加1,很关键，避免快照安装一直循环(知道下次快照)，这里加不加1最多影响到回滚次数多一次还是少一次
            //如果不加1，先dead在activate，那么log的size一直都是lastincludedindx，next = conflict = last一直循环，
            //知道下次超过maxstate，kvserver发起新快照才行
            reply.m_conflict_index = lastIndex() + 1;    
            printf(" [%d]'s logs.size : %d < [%d]'s prevLogIdx : %d, ret conflict idx is %d\n", 
                m_peerId, lastIndex(), args.m_leaderId, args.m_prevLogIndex, reply.m_conflict_index);
            m_lock.unlock();
            reply.m_success = false;
            return reply;
        }
        //走到这里必然有日志，且prevLogIndex > 0
        if(m_logs[idxToCompressLogPos(args.m_prevLogIndex)].m_term != args.m_prevLogTerm){
            printf(" [%d]'s prevLogterm : %d != [%d]'s prevLogTerm : %d\n", m_peerId, m_logs[idxToCompressLogPos(args.m_prevLogIndex)].m_term, args.m_leaderId, args.m_prevLogTerm);

            reply.m_conflict_term = m_logs[idxToCompressLogPos(args.m_prevLogIndex)].m_term;
            for(int index = m_lastIncludedIndex + 1; index <= args.m_prevLogIndex; index++){
                if(m_logs[idxToCompressLogPos(index)].m_term == reply.m_conflict_term){
                    reply.m_conflict_index = index;                         //找到冲突term的第一个index,比索引要加1
                    break;
                }
            }
            m_lock.unlock();
            reply.m_success = false;
            return reply;
        }
    }
    //走到这里必然PrevLogterm与对应follower的index处term相等，进行日志覆盖
    int logSize = lastIndex();
    for(int i = args.m_prevLogIndex; i < logSize; i++){
        m_logs.pop_back();
    }
    // m_logs.insert(m_logs.end(), recvLog.begin(), recvLog.end());
    for(const auto& log : recvLog){
        push_backLog(log);
    }
    saveRaftState();
    if(m_commitIndex < args.m_leaderCommit){
        m_commitIndex = min(args.m_leaderCommit, lastIndex());
        // m_commitIndex = args.m_leaderCommit;
    }
    // for(auto a : m_logs) printf("%d ", a.m_term);
    // printf(" [%d] sync success\n", m_peerId);
    m_lock.unlock();
    reply.m_success = true;
    return reply;
}

pair<int, bool> Raft::getState(){
    pair<int, bool> serverState;
    m_lock.lock();
    serverState.first = m_curTerm;
    serverState.second = (m_state == LEADER);
    m_lock.unlock();
    return serverState;
}

void Raft::kill(){
    dead = 1;
    printf("raft%d is dead\n", m_peerId);
} 

void Raft::activate(){
    dead = 0;
    printf("raft%d is activate\n", m_peerId);
}

StartRet Raft::start(Operation op){
    StartRet ret;
    m_lock.lock();
    RAFT_STATE state = m_state;
    if(state != LEADER){
        // printf("index : %d, term : %d, isleader : %d\n", ret.m_cmdIndex, ret.m_curTerm, ret.isLeader == false ? 0 : 1);
        m_lock.unlock();
        return ret;
    }

    LogEntry log;
    log.m_command = op.getCmd();
    log.m_term = m_curTerm;
    push_backLog(log);

    ret.m_cmdIndex = lastIndex();
    ret.m_curTerm = m_curTerm;
    ret.isLeader = true;
    // printf("index : %d, term : %d, isleader : %d\n", ret.m_cmdIndex, ret.m_curTerm, ret.isLeader == false ? 0 : 1);
    m_lock.unlock();
    
    return ret;
}

void Raft::printLogs(){
    for(auto a : m_logs){
        printf("logs : %d\n", a.m_term);
    }
    cout<<endl;
}

void Raft::serialize(){
    string str;
    str += to_string(this->persister.cur_term) + ";" + to_string(this->persister.votedFor) + ";";
    str += to_string(this->persister.lastIncludedIndex) + ";" + to_string(this->persister.lastIncludedTerm) + ";";
    for(const auto& log : this->persister.logs){
        str += log.m_command + "," + to_string(log.m_term) + ".";
    }
    string filename = "persister-" + to_string(m_peerId);
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT, 0664);
    if(fd == -1){
        perror("open");
        exit(-1);
    }
    int len = write(fd, str.c_str(), str.size());
    close(fd);
}

bool Raft::deserialize(){
    string filename = "persister-" + to_string(m_peerId);
    if(access(filename.c_str(), F_OK) == -1) return false;
    int fd = open(filename.c_str(), O_RDONLY);
    if(fd == -1){
        perror("open");
        return false;
    }
    int length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char buf[length];
    bzero(buf, length);
    int len = read(fd, buf, length);
    if(len != length){
        perror("read");
        exit(-1);
    }
    close(fd);
    string content(buf);
    vector<string> persist;
    string tmp = "";
    for(int i = 0; i < content.size(); i++){
        if(content[i] != ';'){
            tmp += content[i];
        }else{
            if(tmp.size() != 0) persist.push_back(tmp);
            tmp = "";
        }
    }
    persist.push_back(tmp);
    this->persister.cur_term = atoi(persist[0].c_str());
    this->persister.votedFor = atoi(persist[1].c_str());
    this->persister.lastIncludedIndex = atoi(persist[2].c_str());
    this->persister.lastIncludedTerm = atoi(persist[3].c_str());
    vector<string> log;
    vector<LogEntry> logs;
    tmp = "";
    for(int i = 0; i < persist[4].size(); i++){
        if(persist[4][i] != '.'){
            tmp += persist[4][i];
        }else{
            if(tmp.size() != 0) log.push_back(tmp);
            tmp = "";
        }
    }
    for(int i = 0; i < log.size(); i++){
        tmp = "";
        int j = 0;
        for(; j < log[i].size(); j++){
            if(log[i][j] != ','){
                tmp += log[i][j];
            }else break;
        }
        string number(log[i].begin() + j + 1, log[i].end());
        int num = atoi(number.c_str());
        logs.push_back(LogEntry(tmp, num));
    }
    this->persister.logs = logs;
    return true;
}

void Raft::readRaftState(){
    //只在初始化的时候调用，没必要加锁，因为run()在其之后才执行
    bool ret = this->deserialize();
    if(!ret) return;
    this->m_curTerm = this->persister.cur_term;
    this->m_votedFor = this->persister.votedFor;

    for(const auto& log : this->persister.logs){
        push_backLog(log);
    }
    printf(" [%d]'s term : %d, votefor : %d, logs.size() : %d\n", m_peerId, m_curTerm, m_votedFor, m_logs.size());
}

void Raft::saveRaftState(){
    persister.cur_term = m_curTerm;
    persister.votedFor = m_votedFor;
    persister.logs = m_logs;
    persister.lastIncludedIndex = this->m_lastIncludedIndex;
    persister.lastIncludedTerm = this->m_lastIncludedTerm;
    serialize();
}

void Raft::setSendSem(int num){
    m_sendSem.init(num);
}
void Raft::setRecvSem(int num){
    m_recvSem.init(num);
}

bool Raft::waitSendSem(){
    return m_sendSem.wait();
}
bool Raft::waitRecvSem(){
    return m_recvSem.wait();
}
bool Raft::postSendSem(){
    return m_sendSem.post();
}
bool Raft::postRecvSem(){
    return m_recvSem.post();
}

ApplyMsg Raft::getBackMsg(){
    return m_msgs.back();
}

bool Raft::ExceedLogSize(int size){
    bool ret = false;

    m_lock.lock();
    int sum = 8;
    for(int i = 0; i < persister.logs.size(); i++){
        sum += persister.logs[i].m_command.size() + 3;
    }
    ret = (sum >= size ? true : false);
    if(ret) printf("[%d] in Exceed the log size is %d\n", m_peerId, sum);
    m_lock.unlock();
    
    return ret;
}

void Raft::recvSnapShot(string snapShot, int lastIncludedIndex){
    m_lock.lock();

    if(lastIncludedIndex < this->m_lastIncludedIndex){
        return;
    }
    int compressLen = lastIncludedIndex - this->m_lastIncludedIndex;
    printf("[%d] before log.size is %d, compressLen is %d, lastIncludedIndex is %d\n", 
                 m_peerId, m_logs.size(), compressLen, m_lastIncludedIndex);

    printf("[%d] : %d - %d = compressLen is %d\n", m_peerId, lastIncludedIndex, this->m_lastIncludedIndex, compressLen);
    this->m_lastIncludedTerm = m_logs[idxToCompressLogPos(lastIncludedIndex)].m_term;
    this->m_lastIncludedIndex = lastIncludedIndex;

    vector<LogEntry> tmpLogs;
    for(int i = compressLen; i < m_logs.size(); i++){
        tmpLogs.push_back(m_logs[i]);
    }
    m_logs = tmpLogs;
    printf("[%d] after log.size is %d\n", m_peerId, m_logs.size());
    //更新了logs及lastTerm和lastIndex，需要持久化
    persister.snapShot = snapShot;
    saveRaftState();
    
    saveSnapShot();
    printf("[%d] persister.size is %d, lastIncludedIndex is %d\n", m_peerId, persister.logs.size(), m_lastIncludedIndex);
    m_lock.unlock();

}

int Raft::idxToCompressLogPos(int index){
    return index - this->m_lastIncludedIndex - 1;
}

bool Raft::readSnapShot(){
    string filename = "snapShot-" + to_string(m_peerId);
    if(access(filename.c_str(), F_OK) == -1) return false;
    int fd = open(filename.c_str(), O_RDONLY);
    if(fd == -1){
        perror("open");
        return false;
    }
    int length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char buf[length];
    bzero(buf, length);
    int len = read(fd, buf, length);
    if(len != length){
        perror("read");
        exit(-1);
    }
    close(fd);
    string snapShot(buf);
    persister.snapShot = snapShot;
    return true;
}

void Raft::saveSnapShot(){
    string filename = "snapShot-" + to_string(m_peerId);
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT, 0664);
    if(fd == -1){
        perror("open");
        exit(-1);
    }
    int len = write(fd, persister.snapShot.c_str(), persister.snapShot.size() + 1);
    close(fd);
}

void Raft::installSnapShotTokvServer(){
    // while(1){
    //     m_lock.lock();
    //     installSnapShotFlag = true;
    //     printf("%d install to kvserver, install is %d but apply is %d\n", 
    //         m_peerId, installSnapShotFlag ? 1 : 0, applyLogFlag ? 1 : 0);
    //     if(applyLogFlag){
    //         m_lock.unlock();
    //         usleep(1000);
    //         continue;
    //     }
    //     break;
    // }  
    m_lock.lock();
    bool ret = readSnapShot();

    if(!ret){
        // installSnapShotFlag = false;
        // applyLogFlag = true;
        m_lock.unlock();
        return;
    }

    ApplyMsg msg;
    msg.commandValid = false;
    msg.snapShot = persister.snapShot;
    msg.lastIncludedIndex = this->m_lastIncludedIndex;
    msg.lastIncludedTerm = this->m_lastIncludedTerm;

    m_lastApplied = m_lastIncludedIndex;
    m_lock.unlock();

    waitRecvSem();
    m_msgs.push_back(msg);
    postSendSem();

    // m_lock.lock();
    // installSnapShotFlag = false;
    // applyLogFlag = true;
    // m_lock.unlock();
    printf("%d call install RPC\n", m_peerId);
}

int Raft::lastIndex(){
    return m_lastIncludedIndex + m_logs.size();
}

int Raft::lastTerm(){
    int lastTerm = m_lastIncludedTerm;
    if(m_logs.size() != 0){
        lastTerm = m_logs.back().m_term;
    }
    return lastTerm;
}

// int main(int argc, char* argv[]){
//     if(argc < 2){
//         printf("loss parameter of peersNum\n");
//         exit(-1);
//     }
//     int peersNum = atoi(argv[1]);
//     if(peersNum % 2 == 0){
//         printf("the peersNum should be odd\n");
//         exit(-1);
//     }
//     srand((unsigned)time(NULL));
    // vector<PeersInfo> peers(peersNum);
    // for(int i = 0; i < peersNum; i++){
    //     peers[i].m_peerId = i;
    //     peers[i].m_port.first = COMMOM_PORT + i;
    //     peers[i].m_port.second = COMMOM_PORT + i + peers.size();
    //     // printf(" id : %d port1 : %d, port2 : %d\n", peers[i].m_peerId, peers[i].m_port.first, peers[i].m_port.second);
    // }

//     Raft* raft = new Raft[peers.size()];
//     for(int i = 0; i < peers.size(); i++){
//         raft[i].Make(peers, i);
//     }

//     usleep(400000);
//     for(int i = 0; i < peers.size(); i++){
//         if(raft[i].getState().second){
//             for(int j = 0; j < 1000; j++){
//                 Operation opera;
//                 opera.op = "put";opera.key = to_string(j);opera.value = to_string(j);
//                 raft[i].start(opera);
//                 usleep(50000);
//             }
//         }else continue;
//     }
//     usleep(400000);
//     for(int i = 0; i < peers.size(); i++){
//         if(raft[i].getState().second){
//             raft[i].kill();
//             break;
//         }
//     }

//     while(1);
// }

#endif