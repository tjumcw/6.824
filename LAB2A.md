### 6.824分布式系统C++实现—————LAB2A:RAFT超时选举和定时心跳



​		注：LAB2的Raft算法实现细节多到爆炸，而且没有官方最基本的代码支撑，需要多级线程，实现起来相较于goroutine实在是麻烦了很多，该算法涉及同步和定时的任务用C++处理起来也比go麻烦了不少，RPC的处理相比于LAB2指导中封装好的go的RPC库，需要自己处理很多东西，包括string在类中的序列化以及vector的整合以及重新分配，因为我用的RPC库仅支持基本数据类型以及封装这些基本类型的数据对象的传输，最难的是没有各种测试情况只能自己一点一点调试了，看到这，如果你还是坚持C++，希望能多读几遍论文，还有Raft官网的可视化。



### 1、前期准备

- Raft论文：[In Search of an Understandable Consensus Algorithm](http://211.81.63.2/cache/6/03/nil.csail.mit.edu/e67c3fbde658ec109455b8dd31550a83/raft-extended.pdf)建议多读几遍，尤其Section5以及Figure2，很重要，基本涵括了所有的细节。
- Raft官网的可视化：[Raft Consensus Algorithm](https://raft.github.io/)可以自己设置超时模拟选举的各种情况，包括后续LAB中的日志同步以及持久化相关数据的变化，建议看完论文后自己去多试试，可以弄明白论文有些语焉不详的细节。
- Raft演示动画：[Raft (thesecretlivesofdata.com)](http://thesecretlivesofdata.com/raft/)会讲解整个Raft算法的流程，从选举到日志同步都有涉及。
- MIT6.824视频，尤其有关Raft的第二个视频，很多干货，涉及选举更严格的要求以及如何回滚日志(快速回滚在LAB2C)。
- RPC库，如果不想使用<rpc/rpc.h>中的RPC实现，建议去看我MapReduce的说明文档进行RPC库的安装，Raft中使用到了大量的RPC通信，建议熟练运用。
- 6.824仓库里的raft.go，虽然大部分LAB福利C++选手享受不到，但一些注释以及各种class以及函数的说明可以借鉴，虽然实现起来可能大相径庭。助教那节代码可也很关键，虽然是go但是选举时候的条件变量处理方式可以借鉴。

- 附上我自己实现的代码中关于各种类设计的部分，后续会讲讲实现的细节，因为是做完LAB2A、B、C后才写的文档，一些涉及日志和持久化的部分可以暂且忽略，其中locker类我自己封装了一下，主要用到很多锁和条件变量，每次写pthread_mutex/cond_***太麻烦了，很简单就不贴出来了。可以看到我写了9个不同的thread_work函数，在C++中要声明成static类型，很多都是一个线程在开n - 1个线程处理RPC，RPC处理过程中涉及到日志同步以及持久化操作，还需要再开线程分离处理，不然会影响响应速度可能会导致timeout重新选举，这些都是在code的过程中一点一点调试出来的经验，反正很痛苦就是了=-=。其实里面很多部分可以再继续封装，但是暂时不去考虑了。

```c++
//一些用到的class

//单个日志条目，LAB2A中可以暂且忽略
class LogEntry{
public:
    LogEntry(string cmd = "", int term = -1):m_command(cmd),m_term(term){}
    string m_command;
    int m_term;
};

//持久化用到的类，暂且可忽略
class Persister{
public:
    locker persist_lock;
    vector<LogEntry> logs;
    int cur_term;
    int votedFor;
};

//处理超时相关的类，本来写在Raft中的，想想各个data和Raft其他成员比较独立，封装出来比较整洁
class TimeOutInfo{
public:
    TimeOutInfo(){
        m_curHeartBeat_index = 0;
        m_nextHeartBeat_index = 1;
    }
    int m_curHeartBeat_index;
    int m_nextHeartBeat_index;
    locker m_timeout_lock;
    cond m_timeout_cond;
};

//发起添加日志请求的RPC，用于日志同步，若日志为空就是定时心跳，用于leader维护自己权威防止其他follower超时
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

//各个server应答leader发起的AppendEntriesRPC的类，m_conflict_相关的用于快速回滚，LAB2C会用到
class AppendEntriesReply{
public:
    int m_term;
    bool m_success;
    int m_conflict_term;
    int m_conflict_index;
};

//封装了各个server暴露出来的RPC端口号以及自己对应的RaftId
class PeersInfo{
public:
    int m_port;
    int m_peerId;
};

//cancidate发起的投票RPC，也是选举部分最关键的信息，lastLog相关涉及后续LAB中选举规则的细化，暂且忽略
class RequestVoteArgs{
public:
    int term;
    int candidateId;
    int lastLogTerm;
    int lastLogIndex;
};

//投票RPC的应答，会返回自己当前的term和是否投票的信息
class RequestVoteReply{
public:
    int term;
    bool isAccepted;
};

//最关键的就是Raft类，上面的class都是前置声明，因为可能会在Raft中有它们的对象
//就不一一说明了，看明白论文自己去真正实现的时候，发现不明白了再来看，自然就知道我想干嘛了
class Raft{
public:
    static void* listen(void* arg);	//每个Raft都需要创建这个分离的listen线程，用作RPCserver，阻塞
    static void* processEntries(void* arg);   //很关键，守护线程，死循环进行心跳
    static void* waitHeartBeart(void* arg);   //很关键，守护线程，死循环超时处理
    static void* election(void* arg);
    static void* myWait(void* arg);
    static void* worker(void* arg);
    static void* myAppend(void* arg);
    static void* apply(void* arg);
    static void* save(void* arg);
    enum RAFT_STATE {LEADER = 0, CANDIDATE, FOLLOWER};
    void init(vector<PeersInfo> peers, int id);
    pair<int, bool> getState(int term, bool isLeader);
    void waitTime();
    void attemptElection();
    bool callRequestVote();
    RequestVoteReply requestVote(RequestVoteArgs args);   //RPC需要绑定的函数
    bool appendEntries();
    AppendEntriesReply sendAppendEntries(AppendEntriesArgs args);   //RPC需要绑定的函数
    bool checkLogUptodate(int term, int index);
    void push_backLog(LogEntry log);
    vector<LogEntry> getCmdAndTerm(string text);
    StartRet start(string command);
    void applyToStateMachine();
    int getRandTime();
    void serialize();
    bool deserialize();
    void saveRaftState();
    void readRaftState();
    void run();
    bool isKilled();  //->check is killed?
    void kill();   
    void initLog(vector<LogEntry>& log);

private:
    locker m_lock;
    locker m_leader_lock;
    locker m_commit_lock;
    cond m_cond;
    cond m_leader_cond;
    cond m_commit_cond;
    vector<PeersInfo> m_peers;
    int m_isVoteFinished;
    int m_peerId;
    int m_curTerm;
    bool m_killed;
    int m_votes;
    int m_votedFor;
    bool m_voted;
    int cur_peerId;
    int m_lastApplied;
    int m_commitIndex;
    Persister persister;
    unordered_map<int, int> m_firstIndexOfEachTerm;
    vector<int> m_nextIndex;
    vector<int> m_matchIndex;
    vector<LogEntry> m_logs;
    RAFT_STATE m_state;
    TimeOutInfo m_timeout;   //处理超时相关的类
};

```



### 2、初始化

- 初始化主要传入的参数有peers数组和自己在peers中的ID，peers封装了各个peer也就是每个raft对象暴露给client的RPC端口号，可以看上文中关于PeersInfo类的定义。需要注意的是，每个Raft实例中传入的peers是一样的，但ID是从0~peers.size() - 1各不相同的。这样每个raft都包含了其他raft示例的RPC端口，并取得了自己唯一的peerID，在自己成为leader或candidate时可以通过创建多个线程向peers中除了自己以外的raft对象发起RPC请求(goroutine中很方便，C++线程实现需要额外维护一个cur_peerId表示哪些peer是已经发过的)

```c++
void Raft::init(vector<PeersInfo> peers, int id){
    m_curTerm = 0;
    m_peers = peers;
    m_peerId = id;
    m_peers = peers;
    //m_nextIndex.resize(peers.size(), 0);
    //m_matchIndex.resize(peers.size(), 0);
    //m_logs.clear();
    //m_firstIndexOfEachTerm.clear();
    cur_peerId = 0;
    m_votedFor = -1;
    m_voted = false;
    m_votes = 0;
    m_isVoteFinished = 0;
    //m_lastApplied = 0;
    //m_commitIndex = 0;
    m_state = FOLLOWER;
    m_killed = false;
    //readRaftState();
    run();
}
```

- 每个Raft实例需要创建listen线程并启动RPC的server服务，之所以再创建一个线程是为了不阻塞当前线程，可以看到作为RPCserver绑定了requestVote以及sendAppendEntries两个函数。在listen线程中还需要创建两个长期运行的守护线程waitHeartBeart以及processEntries，分别用来等待心跳即处理超时相关逻辑，以及处理日志条目，即发起心跳的逻辑。

```c++
void Raft::run(){
    pthread_t tid;
    pthread_create(&tid, NULL, listen, this); //传this指针应该是多线程中常规操作了，不懂的自己百度
    pthread_detach(tid);
}

void* Raft::listen(void* arg){
    Raft* raft = (Raft*)arg;  //获取传入的this指针，即获得了对象的首地址可以操作对象
    buttonrpc server;
    server.as_server(raft->m_peers[raft->m_peerId].m_port);
    server.bind("requestVote", &Raft::requestVote, raft);
    server.bind("sendAppendEntries", &Raft::sendAppendEntries, raft);
    pthread_t wait_tid;
    pthread_create(&wait_tid, NULL, waitHeartBeart, raft);
    pthread_detach(wait_tid);
    pthread_t heart_tid;
    pthread_create(&heart_tid, NULL, processEntries, raft);
    pthread_detach(heart_tid);
    server.run();
    printf("exit!\n");  //这句话实际上是不可能执行的，因为server.run()后阻塞了
}
```

之后的内容是核心部分了，遵循课程的要求我就不放关键代码了，但后续我会讲一些C++实现起来较麻烦的关键点。



### 3、选举细节

![image](https://user-images.githubusercontent.com/106053649/171131716-b5f664c7-8775-4376-a4db-a71abe51a8aa.png)

​							相信我，这张图会陪伴你很长时间的，LAB2A中可以重点关注两个RPC部分的细节



1、超时时间的设置

- 超时时间的选择
  - 在Raft论文中提出，初始化时所有的server都是FOLLOWER，都会开始进行超时时间的统计，一旦超时，就会发起选举。论文中是设置随机超时时间，范围在150ms~300ms，这样可以很大程度上避免很多server同时选举导致分票不能获取majority的选票，这会导致一个term内没有leader，这是可以接受的，一个term内至多有一个leader，无非是再进行一轮选举罢了，随机超时时间可以很大程度上避免这种情况的连续出现。
  - 一个经验：设置随机种子时在主线程(测试时)中设一个即可，不需要每个Raft设置一个，这样在多线程中由于CPU多核会导致所有的randTime相同，相反一个随机种子调用时间不同产生的随机数也不相同。
  - 不需要参考论文的150~300ms，实际上LAB的指导中推荐时间相对长一些比如200~400ms，这些会比较好处理(各个机器的性能不同，所以没必要照搬论文)。
- 超时时间的重置
  - 建议打开Raft官网可视化进行模拟，一开时1，2，3，4，5启动，都在计时。若3先超时发起选举，并获得了大多数的选票，那么3成为leader并马上发起心跳维护权威，收到心跳的server重置自己的超时时间。需要特别指出的是，**一旦某个server收到投票请求RPC并投票，那么也会重置该server的超时时间**，这在论文中没有明确指出，但在官网的可视化中我们可以很清楚地模拟出这一点。即**发出投票或收到合法心跳**，都会重置超时时间。这里的合法，LAB2A中主要是指自己的term和发起RPC请求的term大小比较，详细看论文。
- 一些很细的细节
  - 一个server在一个term内只能投一票，但不是说一段时间内只能投一篇，通过官网可视化我得出这样的结论。比如有3个server，为S1,S2,S3，他们的term分别问1，2，3(虽然很难出现，但实际上存在这种可能且可以实际模拟出来)。如S2和S3都超时，且都向S1发起投票RPC即调用requestVote()，若S1先收到S3的请求，那么给3投票，并将自己的term置为3，那么当S1收到S2请求时就会拒绝投票，因为自己的term大于S2的term。但若是S1先收到S2请求，那么给我投票并置自己term为2，若再收到S3请求，依然应该投票，因为S3的term为3>2，虽然这种情况下S1投了两票，但分别是在不同的term下投出的，实际的Raft在这种情况下也是这么做的。



2、道理我都懂，具体实现

- 首先考虑到上述讲到重置超时时间的两种可能，一种是投票，一种是有效心跳，那么就可以很好的利用我们的条件变量中pthread_cond_timedwait()的API来进行处理，具体细节不说，实现方法可以参考我在**MapReduce**中有关定时任务的介绍，我们只需要传入timespec类型的对象，该对象记录的是我们的超时时间，并且分别在上述两种情况下会发起pthread_cond_signal()来解除阻塞，也就是说结束阻塞的情况一共有3种，只需分别设置相关变量加以判断即可，条件变量加判断的逻辑，应该是多线程同步常用的技巧了，无非这里的情况相对较多，需要理清楚。

- 可以看到我在Raft类中还有一把leader_lock，其实也就是为了能利用条件变量在成为leader时唤醒processEntries守护线程中的代码，这和选举过程具体实现无关，但又依赖于选举结果，一旦成为leader要先发心跳维护权威防止其他Raft实例超时。心跳周期是100ms，建议把睡眠放在心跳之后，因为成为leader需要马上发起心跳重置其他Raft的超时时间。具体实现起来有一些技巧，因为考虑到leader可能会过时，设计processEntries内部逻辑时需要考虑这一点。

- LAB2A中是否同意投票的细节我就不赘述了，只要看论文认真思考应该很容易得到答案，结果不正确建议再去看看Raft官网的可视化，那真的很酷，可以帮助你理解论文中Figure2的各个细节。

- 还有一个细节，这段代码在助教讲解go的视频中有类似代码我就放出来我的实现了，主要是用于投票时统计结果用的，用条件变量能很好的避免while空转，它不会影响CPU时间，这也是我在Raft实现中用到的第三个条件变量了，记得上文要加锁，用完解锁，getLock()是我自己封装的函数，不用在意。语义就是每获得一张票就会signal()，这里我没写出来，判断是否大于一半的选票，或者是否所有人都投完票了，很好理解。

  ```c++
  while(m_votes <= m_peers.size() / 2 && m_isVoteFinished != m_peers.size()){
      m_cond.wait(m_lock.getLock());
  }
  ```



### 4、总结

​		LAB2A是LAB2的基本框架实现，很关键，LAB2B就是对各个模块功能的晚上，LAB2C是一些细节的优化以及持久化处理。在这个LAB中会大量运用到多线程以及RPC，要用好互斥锁以及条件变量，强烈推荐条件变量的timedwait()而不是简单的wait()，它在实际业务中更常用，因为不可能总是在长时间等待cond的满足，若是持有锁的服务器crash怎么办，定时条件变量这很有效果，在我第一篇LAB1的MapReduce有详细介绍定时处理的相关方法，有需要可以去看一下，祝LAB2A码得愉快！
