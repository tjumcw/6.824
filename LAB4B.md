### 6.824分布式系统C++实现—————LAB4B:最终的分布式shard DB

注：

- 最后一个LAB应该是整个系列中难度最大的了，raft的实现虽然也很痛苦但有论文以及MIT的学生指导资料可以参考，需要结合之前造的所有轮子
- 需要实现LAB4A中shardClerk与shardMaster间的通信，其中包括
  - Clerk内的shardClerk与shardMaster间的通信，更改和查询配置信息config
  - shardKv内的shardClerk与shardMaster，查询配置信息config
- 需要实现不同groups间的通信，主要有
  - 数据迁移，拉取新数据给自己（由于config更新，有新的shard由自己负责，需要去上一个config拉取负责该分片的数据）
  - 垃圾回收，需要告知送出数据方的group自己已成功接收，让其清理对应分片的数据

![img](https://pic1.zhimg.com/80/v2-716309226bd71727ea06fb9d26c3c86c_720w.jpg)

- 参考上图的模式，图片来自[MIT6.824 Lab4 Sharded Key/Value Service - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/422919910)
- 建议配合LAB4A的md文件食用，主要是对于系统不同粒度的分析(client和每个集群的每个server都有一个shardClerk对象)

- 理解：
  - client与单独的kvServer之间的运行模式即LAB3实现的系统
  - Kvserver的集群间需要RPC通信交换数据
  - 每个config对应一个shard在groups内的分配情况，如上图若有3个集群G1,G2,G3，若config为[1, 1, 1, 2, 2, 2, 3, 3, 3, 3]
    - 即group1的集群负责分片1，2，3，group2的集群负责4，5，6，group3的集群负责7，8，9，10
    - 每个集群的leader会定期查询配置，若配置变化，需要集群间通信，若变为[1, 1, 1, 2, 2, 2, 3, 3, 4, 4],即加入了新的集群group4，则
      - 1、集群4要向原先负责分片shard9，10的集群group3发送RPC，拉取对应分片的数据库数据以及一些状态信息
      - 2、集群4收到集群3的reply后，向集群3发送清理分片9，10的RPC，必须要接收方确认收到数据才能让发送方删除数据
  - client如何将特定的key定向到正确的分片以及负责该分片的集群
    - 通过key2shard函数，将string类型的key映射到特定shard，
    - 通过当前config中的shards得到该shard对应集群的GID
    - 通过当前config中的groups得到该GID对应集群内所有server的名字
    - 通过make_end函数，将server名字对应到其RPC端口
    - 遍历所有server的端口，直到确认是leader并成功受理请求



### 一、client端细节

#### 1、类定义

```c++
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
```

- 由于官方go的测试程序没法用，所以只能自己模拟配置变化情况，在client内重新封装了几个masterClerk的处理函数，方便调用
- 实现的其实同LAB3的client大同小异，也是get、put、append请求，无非多了一个shardClerk对象，可以通过其更新和查询配置
- 基于分片的多集群系统引入的一些变化
  - leaderID不在是一个单独的int变量，因为是基于分片的多集群系统，根据不同的key要定向到不同的集群，需要暂存每个集群的leaderID
  - RPC请求函数相比于LAB3需要先找到对应集群的server，具体可看上文注意中的分析，框架代码如下：

```c++
string Clerk::get(string key){
    GetArgs args;
    args.key = key;
    args.clientId = clientId;
    args.requestId = requestId++;

    while(1){
        int shard = key2shard(key);
        int gid = config.shards[shard];
        if(config.groups.count(gid)){
            vector<string> servers = config.groups[gid];
            for(int i = 0; i < servers.size(); i++){
     			//......  
                int curLeader = (i + leaderId[gid]) % servers.size();
                int port = make_end(servers[curLeader]);
                GetReply reply = client.call<GetReply>("get", args).val();
                //......
            }
        }   
        usleep(100000);
        config = masterClerk.Query(-1);    
    }   
}
```



#### 2、其他注意点

- 新增了ErrWrongGroup的可能，即key定向到错误的集群
- 其他处理以及思路可以完全照搬LAB3的实现



### 二、server端细节

注：server端实现的细节比较多，会着重分析类的定义，由于一开始没有考虑challenge的垃圾回收，后续在做challenge时造成了很大的困难，主要是没理清回收时机的选择以及回收确认的机制，后来参考了[8.MIT 6.824 LAB 4B(分布式shard database) - 简书 (jianshu.com)](https://www.jianshu.com/p/f5c8ab9cd577)并借鉴了其垃圾回收的处理方式

#### 1、类的定义

```c++
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

    void StartKvServer(vector<kvServerInfo>& kvInfo, int me, int gid, int maxRaftState, vector<vector<int>>& masters);
    void getPutAppendOnDataBase(ShardKv* kv, Operation operation, OpContext* opctx, bool isOpExist, bool isKeyExisted, int prevRequestIdx);
    void doSnapShot(ShardKv* kv, ApplyMsg msg);

    GetReply get(GetArgs args);
    PutAppendReply putAppend(PutAppendArgs args);
    MigrateRpcReply shardMigration(MigrateArgs args);
    GarbagesCollectReply garbagesCollect(GarbagesCollectArgs args);

    bool isMatchShard(string key);
    void updateComeInAndOutShrads(Config config);
    void updateDataBaseWithMigrateReply(MigrateReply reply);
    void clearToOutData(int cfgNum, int shard);
    // string test(string key){ return m_database[key]; }  //测试其余不是leader的server的状态机

    string getSnapShot();
    void recoverySnapShot(string snapShot);
    void printSnapShot();

    //----------------------------test------------------------------------
    bool getRaftState();
    void killRaft();
    void activateRaft();

private:
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
    unordered_map<int, unordered_set<int>> garbages;
    unordered_map<int, unordered_set<int>> garbagesBackUp;

    int garbageFinished;
    int garbageConfigNum;
    int pullShardFinished;

    unordered_set<int>::iterator garbageIter;
    unordered_map<int, int>::iterator pullShardIter;

};
```

- 3个新的守护线程：每个集群的每个server都在运行，但只有leader能进行处理

  - updateConfigLoop
    - 定时通过类内的shardClerk对象Query新的Config，如果Config.Num大于自身，则提交一个 NewConfigOp 给Raft
    - 集群内的所有server在Raft Apply到应用层后之后一起安装这个NewConfig，保证一致性。
    - 如果新的config. shards相对于以前有变化，则可能会有关于自身的数据迁移需求
      - 数据迁入：即原先shards内某一不属于自己GID的分片在新的config中属于自己了，需要拉取数据
      - 数据迁出：即原先shards内属于自己GID的分片在新的config中不属于自己了，需要等待拉取方的RPC请求
    - 数据迁移需要考虑几个条件
      - shard不归自己，返回ErrWrongGroup
      - shard如果正在迁移过程中，即暂时不可用，返回ErrWrongLeader
  - pullShardLoop
    - 同样定时查询自己的集群是否需要拉取数据
    - 考虑维护一个实时变化的数据结构暂存当前server需要拉取的数据对应的shard
  - garbagesCollectLoop
    - 同样定时查询自己的集群是否需要发送垃圾清理的RPC
    - 考虑维护一个实时变化的数据结构暂存当前server需要告知其他集群需要清理的分片数据

- 由上述分析可知，需要有几个关键的数据结构，在LAB4B中的数据迁移以及垃圾回收都采取pull模式

  ```c++
      unordered_map<int, unordered_map<int, unordered_map<string, string>>> toOutShards;
      unordered_map<int, int> comeInShards;
      unordered_set<int> m_AvailableShards;
  ```

  - toOutShards：需要送出去的分片
    - 从configNUm -> shard -> (key, value)
    - 一定要有configNum，因为配置变化后数据归属发生了变化，而且数据迁移的同时配置也会发生变化，3个Loop并不是同步等候的形式
    - 利用configNum对应到那个时刻的DB，在网络通信不佳的情况下也可以很好处理，避免数据来回迁移过程(送出去，一段时间更新DB后又送回来)中数据的重复以及覆盖(没有configNum的话同一个分片的DB数据只能有一份，若是在上述的来回送的极端情况下RPC丢失后，很长时间才重发可能拿到不是对应时刻的数据了)
    - 有了configNum在RPC发起端才可以利用configNum通过shardClerk的Query()查询到对应config，在将shard对应到数据迁移那个时刻负责该分片的特定集群

  - comeInShards：需要拉取的分片
    - shard -> configNum
    - 对应toOutShards，需要拿到特定config时的shard数据
  - m_AvailableShards：当前可用的分片
    - 数据迁移过程中，存在comeInShards中的shard暂且不可为server端服务，一旦接收到数据转为可用
    - 一旦该集群内的某个shard需要被送出去，也会移除其在该map中对应的元素，无需等待RPC结果直接不可用



### 3、具体流程分析

- 初始时刻config Num为0，groups为空，shards为[0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
- 所有的集群内的所有server都在updateConfigLoop中，但只有server并且在无数据需要拉取的情况下定时查询配置信息，若有数据拉取则先需要完成数据迁移才能继续查询
- 某一时刻，若加入了GID = 2，且group = ["21", "22", "23", "24", "25" ]的集群，则num为1的config的shards为[2, 2, 2, 2, 2, 2, 2, 2, 2, 2]
- 因为初始为空（特例），所以暂且不需要拉取数据，集群2的所有server的comeInShards仍未空,此时config.num = 1
- 若再加入GID = 1及GID = 3，则新的config.num = 2，且按我写的负载均衡后的shards为[2, 2, 2, 2, 3, 1, 3, 1, 3, 1]
- 此时comeInShards为G1[5, 7, 8]，G3[4, 6, 8]
- 对应G2的toOutShards为configNum = 1 时刻 G2[4, 5, 6, 7, 8 ,9]以及其对应的所有数据
- 此时所有集群的m_AvailableShards分别为G2[0, 1, 2, 3],G1和G3为空
- 在pullShardLoop集群1和集群3检测到comeInShards不在为空，分别向集群2发起doPullShard的RPC请求，一旦RPCreply成功，则
  - 分别将[5, 7, 9]及[4, 6, 8]加入m_AvailableShards
  - 分别将[5, 7, 9]及[4, 6, 8]加入对应configNum = 1的garbages内

- 集群1和集群3的garbagesCollectLoop一旦检测到garbages的大小不为空，则分别向集群2发起垃圾清理的RPC，告知其自己收到了对应的数据，在集群2内可以删除了，这个接收方集群2也是通过configNum查询到对应的config在利用shard查到迁移前分则分片的集群为集群2，所以上面几个数据结构分析处的configNum是非常关键的，一定要把分片对应到configNum才有意义
- 集群2收到垃圾清理的RPC后，清理完toOutShards中对应configNum的shards后，返回清理成功的reply，集群1和3收到后，清理自己的garbages
- 至此，一个配置更新的整体流程就结束了，之后就是在该配置下各个集群分别受理对应分片的请求并处理，将结果写入DB，等待下次配置更新



### 4、实现的关键点

- 主要就是在applyLoop中增加新的分支，原先是

  - 快照分支

  - 涉及DB的get、put、append的操作分支

  - 现在在除了快照分支外的大分支中，除了DB操作，还要涉及updateConfig，updateDB以及garbagesCollect分支

  - 基本结构如下：具体的实现逻辑就不展示了，applyLoop中的其他逻辑部分也都略去了，只留下了骨干分支

    ```c++
    void* ShardKv::applyLoop(void* arg){
        ShardKv* kv = (ShardKv*)arg;
        while(1){
    
            kv->m_raft.waitSendSem();
            ApplyMsg msg = kv->m_raft.getBackMsg();
    
            if(!msg.commandValid){
                kv->doSnapShot(kv, msg);
            }else{
                Operation operation = msg.getOperation();
                if(operation.op == "UC"){
                    Config config = getConfig(operation.value);
                    kv->updateComeInAndOutShrads(config);       //进行更新操作，该函数内部加锁了
                }
                else if(operation.op == "UD"){
                    MigrateReply reply = str2MigrateReply(operation.value);
                    kv->updateDataBaseWithMigrateReply(reply);  //进行更新操作，该函数内部加锁了
                }else{
                    if(operation.op == "GC"){  
                 		int cfgNum = stoi(operation.key);
                        kv->clearToOutData(cfgNum, operation.requestId);
                    }else{
                        kv->getPutAppendOnDataBase(kv, operation, opctx, isOpExist, isSeqExist, prevRequestIdx);
                    }
                    //保证只有存了上下文信息的leader才能唤醒管道，回应clerk的RPC请求(leader需要多做的工作)
                    if(isOpExist){  
                        int fd = open(opctx->fifoName.c_str(), O_WRONLY);
                        char* buf = "12345";
                        write(fd, buf, strlen(buf) + 1);
                        close(fd);
                    } 
                }
            }
            kv->m_raft.postRecvSem();
        }
    }
    ```

- 这些分支也都是通过向底层raft传递相应op的日志来实现的，如

  - updateConfigLoop对应operation.op == "UC"

    - 在whileLoop中一旦发现有新配置就传入raft层

    ```c++
    void* ShardKv::updateConfigLoop(void* arg){
        ShardKv* kv = (ShardKv*)arg;
        while(1){
    		//......
            int nextCfgNum = kv->m_config.configNum + 1;
            Config config = kv->m_masterClerk.Query(nextCfgNum);
            if(config.configNum == nextCfgNum){
    			//......
                kv->m_raft.start(operation);  //也是在自己的集群里调用start，无需考虑线性一致性，那个是对于客户端请求要求保证的
            }
            usleep(50000);
        }
    }
    ```

  - doPullShard对应operation.op == "UD"，

    - 对每个shard都创建线程互不干扰发送拉取数据RPC，一旦收到数据就由leader传入raft层共识后安装到所有server的应用层

    ```c++
    void* ShardKv::doPullShard(void* arg){
        ShardKv* kv = (ShardKv*)arg;    
        MigrateArgs args;
    	//......
    
        Config config = kv->m_masterClerk.Query(cfgNum);
        int gid = config.shards[args.shard];
    
        for(const auto& server : config.groups[gid]){
    		//......
            MigrateRpcReply retReply = client.call<MigrateRpcReply>("shardMigration", args).val();
            MigrateReply reply = str2MigrateReply(retReply.reply);
            if(reply.err == OK){
    			//......
                //每收到一个reply就共识然后通过applyLoop进行DB的更新,是发起方在自己的集群里调raft的start
                //不是在收到RPC的server集群里调用，若是在收到RPC的server集群里调用start就类似kvserver的处理，需要考虑线性一致性
                kv->m_raft.start(operation);         
            }
        }
    	//......
        return NULL;
    }
    ```

  - garbagesCollect的RPC处理函数对应operation.op == "GC"

    - 由applyLoop结构可见GC流程有别于上面2钟，也需要类似LAB3中基本DB操作般，需要通过上下文保证线性一致性，这是因为
      - 上述两种分别是集群内发现配置变更以及RPC发送方收到数据，一旦检测到就传入raft层
      - 而此处是接收方收到请求并受理，清理自己的toOutShards，类似于LAB3的DB请求，是来自其他客户端的请求要应用到自己的状态机上，同上述两种有本质的不同

    ```c++
    GarbagesCollectReply ShardKv::garbagesCollect(GarbagesCollectArgs args){
        GarbagesCollectReply reply;
    	//......逻辑判断
    	//......
    	//......封装操作
    
        StartRet ret = m_raft.start(operation);   //必然是leader，不然上面就return，预防极端情况正好这里宕机了又进行了判断
    
    	//......逻辑处理
    	//......
        //......类似LAB3的处理，见下
        
        OpContext opctx(operation);
        m_lock.lock();
        m_requestMap[ret.m_cmdIndex] = &opctx;
        m_lock.unlock();
    
        Select s(opctx.fifoName);
        myTime curTime = myClock::now();
        while(myDuration(myClock::now() - curTime).count() < 2000000){
            if(s.isRecved){
                break;
            }
            usleep(10000);
        }
        if(s.isRecved){
            if(opctx.isWrongLeader){
                reply.err = ErrWrongLeader;
            }
        }
        else{
            reply.err = ErrWrongLeader;
        }
        m_lock.lock();
        if(m_requestMap.count(ret.m_cmdIndex)){
            if(m_requestMap[ret.m_cmdIndex] == &opctx){
                m_requestMap.erase(ret.m_cmdIndex);
            }
        }
        m_lock.unlock();
        return reply;
    }
    ```

    - 可见代码结构与LAB3很一致，就是代码内容完全不一致了，处理方式照旧

- 剩下的就是方法的实现了，具体到C++中还是比较麻烦的，主要还是无法利用goroutine的便捷性
  - 因为涉及到很多shard，每个shard都要发RPC，线程间同步需要好好处理，比较麻烦
  - 整个流程还是要好好理清，不然写起来云里雾里，C++线程在套线程的方式处理，只能不断增加类内的同步成本
  - 没有6.824官方给出的很多底层库，如RPC实现，端口对应以及非阻塞请求，实际上给的骨架go代码一行的事我自己写都要写很多行代码来模拟实现

- 最后还有快照的实现，有一些新的状态需要写入快照并持久化，就不一一展开讨论了



### 5、总结

整个项目结束了，上面逻辑理清楚了，具体方法的实现就不提供了。

想了想代码还是扔Github吧，主要课程都是go实现的代码，应该不会有MIT的学生去copy代码的时候，放着网上一堆go的实现不去参考去看这一直再增加代码负责度还没有办法运行测试脚本只能自己手动创建bug测试的C++版本吧。

之前写的代码一直扔在学校实验室的服务器上，想了想服务器也老是死机，自己笔记本虚拟机卸了以后再也装不上了，扔GitHub也不错。

整个项目基本就是看视频，看论文，看官方LAB的指导文档，看go的骨架代码，在看指导文档，有问题再回去看视频，实在没办法参考下网上的分析(主要是LAB4，其他基本都是按照自己的理解来写的)，感觉学到了很多，不仅是代码上的，还有处理问题和设计方案的能力，以及分布式系统的知识。

再次感谢MIT6.824这门课，获益匪浅，要继续加油！