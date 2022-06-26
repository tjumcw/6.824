### 6.824分布式系统C++实现—————LAB4A:KvRaft日志压缩

注：

- 首先来回读官方文档[6.824 Lab 4: Sharded Key/Value Service (mit.edu)](http://nil.csail.mit.edu/6.824/2020/labs/lab-shard.html)，读得越多越好，一开始很难理解
- 结合官方给的go的骨架代码，可以连4B部分一起看，这样更容易理解分布式shard database的架构
- 将LAB4A实现的管理分片配置的server及clerk类理解成是服务于LAB4B的
- 一些粒度的理解
  - shardMaster集群：为了保证容错及高可用性
    - 有很多shardMaster，而每个shardMaster下层是一个raft
  - shardClerk
    - 每个shardClerk都是独立的，向shardMaster发送RPC请求更改和查询配置
  - LAB4B中的Clerk
    - 有自己的成员变量及一个shardClerk对象，用以发起常规数据库操作请求以及涉及配置相关操作	
  - LAB4B中的group集群：有很多group
    - 每个group是一个很多server组成的server集群
    - 每个server都有一个shardClerk对象，并且下层都是一个raft，单group内的集群就类似LAB3实现的server集群
- 对于LAB4A及LAB4B整体理解
  - 单独理解LAB4A中的shardMaster集群以及shardClerk，可以看成类似LAB3实现的功能完全不懂的shard配置系统
  - LAB4B的server端有很多集群，单个集群理解成LAB3实现的kvRaft分布式数据库系统再加上一个shardClerk用以查询配置
  - LAB4B的集群间存在数据交互，所以需要在LAB3基础上引入分片配置系统



### 一、在LAB3基础上修改实现分片配置功能

理解Config这个类

```c++
class Config{
public:
    Config(){
        configNum = 0;
        shards.resize(NShards, 0);
        groups.clear();
    }
    int configNum;
    vector<int> shards;
    unordered_map<int, vector<string>> groups;
};
```

- Config的成员变量
  - configNum：表示该配置的配置号，单调递增，每个配置会存放在server端，Query(int num)即按照配置号查询配置
  - shards ： 表示分片分配情况，实验要求一共十个分片，一开始没有group占有即为[0, 0, 0, 0, 0, 0, 0, 0, 0, 0]，需要考虑负载均衡
  - groups : 即负责当前分片的集群，以及集群内所有server的信息，根据server信息能查到其RPC端口，LAB4B中会用到
  - 通常分片的数量会比组更多(即每个组将服务于多个分片)，以便负载可以在一个相当细的粒度上转移。



### 二、client端细节

#### 1、shardClerk类

```c++
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
```

- 两个静态变量是因为需要把他写成hpp作为LABB引入的文件，这样的话全局变量就写为静态，需要类外初始化（功能类似LAB3中的同名变量，对应server端的几个不同PRC端口，让多个请求可以通过不同的端口在server端处理而不是所有请求排队等候）

#### 2、4个shardClerk发起的RPC请求

- Join RPC
  - 用来add新的集群，如初始10个分片对应的gid(groupID)都为0，若加入1个集群会变成[1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
  - 参数为非0的GIDS，映射到server names构成的数组，因为一个集群有很多server，每个集群有一个GID，可以同时加入很多集群
  - 表现方式：shardmaster 会创建新的配置，configNum++，且shards及groups均会改变
  - 新配置应该在整个组中尽可能均匀地分配碎片，并且应该移动尽可能少的碎片来实现该目标，即负载均衡
  - 无需返回值，重试直到成功即可
- Leave PRC
  - 参数为先前已加入组的gid列表。
  - shardmaster应该创建一个不包括这些组的新配置，并将这些组的碎片分配给其他组。
  - 新配置应该在组之间尽可能均匀地分配碎片，并且应该移动尽可能少的碎片来实现这一目标。
  - 无需返回值，重试直到成功即可
- Move RPC
  - 参数是一个分片号和一个GID
  - shardmaster应该创建一个新配置，其中将分片号对应的shard分配给组GID，注意不需要负载均衡，负载均衡会破坏Move的语义。
  - 无需返回值，重试直到成功即可
- Query RPC
  - Query RPC的参数是一个配置号。shardmaster返回具有该数字的配置
  - 如果这个数字是-1或大于最大的已知配置数字，shardmaster应该回复最新的配置。
  - 返回值会对应传入参数号的配置信息

- 具体的RPC请求函数都很类似，贴一个Query的，server端的处理函数后面再server细节的部分在贴

  ```c++
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
  ```

- 其他处理几乎雷同，都是一直请求知道成功，无非是那几个没有返回值，而Query需要返回一个config



### 三、server端细节

#### 1、shardMaster类

```c++
class ShardMaster{
public:
    static void* RPCserver(void* arg);
    static void* applyLoop(void* arg);
    void StartShardMaster(vector<kvServerInfo>& kvInfo, int me, int maxRaftState);
    JoinReply Join(JoinArgs args);
    LeaveReply Leave(LeaveArgs args);
    MoveReply Move(MoveArgs args);
    QueryReply Query(QueryArgs args);
    bool JoinLeaveMoveRpcHandler(Operation operation);      //将更改配置的操作抽象成一个RPC处理函数，因为处理流程几乎一致，只需知道完没完成即可
    void doJoinLeaveMove(Operation operation);              //通过start传入raft共识后再applyLoop中得到的msg还原到operation:Join，Leave，Move进入对应的处理函数
    void balanceWorkLoad(Config& config);                   //对Join和Leave的操作都要进行负载均衡，通过map及2个优先队列实现
    int getConfigsSize();

private:
    locker m_lock;
    Raft m_raft;
    int m_id;
    vector<Config> configs;						//存各个config，一开始需要有一个编号为0的config为初始config

    vector<int> m_port;
    int cur_portId;
    // bool dead;

    unordered_map<int, int> m_clientSeqMap;    //只记录特定客户端已提交的最大请求ID
    unordered_map<int, OpContext*> m_requestMap;  //记录当前RPC对应的上下文
};
```

- 仔细理解shardClerk以及shardMaster类的定义，发现其与LAB3实现的框架结构很类似，虽然内容完全不同，但处理模式是很接近的



#### 2、RPC处理函数，

贴对应上文的Qurey的RPChandler，其他三个处理很类似，而且其他三个可以抽象出一个templateHanlder处理(无返回值只需知道是否成功)

```c++
QueryReply ShardMaster::Query(QueryArgs args){
    QueryReply reply;
    reply.isWrongLeader = false;
    Operation operation;
    operation.op = "Query";
    operation.key = "random";
    operation.value = "random";
    operation.clientId = args.clientId;
    operation.requestId = args.requestId;
    operation.args = to_string(args.configNum);

    StartRet ret = m_raft.start(operation);

    operation.term = ret.m_curTerm;
    operation.index = ret.m_cmdIndex;
    if(ret.isLeader == false){
        // printf("client %d's Query request is wrong leader %d\n", args.clientId, m_id);
        reply.isWrongLeader = true;
        return reply;
    }

    OpContext opctx(operation);
    m_lock.lock();
    m_requestMap[ret.m_cmdIndex] = &opctx;
    m_lock.unlock();

    Select s(opctx.fifoName);
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
        }else{
            // printf("in query rpc cfgNum : %d\n", opctx.config.configNum);
            reply.configStr = getStringFromConfig(opctx.config);
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
```

- 可以看到和LAB3的结构几乎完全一样，在完成LAB3的基础上编写LAB4的代码，发现处处都被安排好了，不得不说6.824神课
- 同样的超时处理以及select(fifo)机制，且在applyLoop中以及同raft层的交互都和LAB3相同，详见LAB3的说明文档



#### 3、LAB3A主要工作：负载均衡

主要思想，利用两个优先队列，分别为大根堆以及小根堆

一些细节：

- config中的groups改变是才需要负载均衡
  - Join需要，groups有新的group加入
  - Leave需要，groups少了旧的group
  - Move不需要，没有修改groups，负载均衡指挥破坏Move的语义

- 何时负载均衡

  - applyLoop中收到raft层共识后的msg，回复其语义原请求，若是修改配置的操作则调用doJoinLeaveMove()，Query直接返回config
  - doJoinLeaveMove()具体实现：根据请求类型进行处理

  ```c++
  void ShardMaster::doJoinLeaveMove(Operation operation){
      Config config = configs.back();         //取得最近的配置，在此基础上更改
      printf("[%d] in doJoinLeaveMove size : %d, num: %d\n", m_id, configs.size(), config.configNum);
      unordered_map<int, vector<string>> newMap;
      for(const auto& group : config.groups){
          newMap[group.first] = group.second;
      }
      config.groups = newMap;
      config.configNum++;
  
      if(operation.op == "Join"){
          unordered_map<int, vector<string>> newGroups = getMapFromServersShardInfo(operation.args);
          for(const auto& group : newGroups){             //加入新config的groups中(gid -> [servers])
              config.groups[group.first] = group.second;
          }
          if(config.groups.empty()){                      //说明此前必然是空的,args也是空的，直接return
              return;
          }
          balanceWorkLoad(config);                        //进行负载均衡
          configs.push_back(config);                      //将最新配置插入configs数组
          return;
      }
      if(operation.op == "Leave"){
          vector<int> groupIds = GetVectorOfIntFromString(operation.args);
          unordered_map<int, int> hash;
          for(const auto& id : groupIds){
              config.groups.erase(id);
              hash[id] = 1;
          }
          for(int i = 0; i < NShards; i++){
              if(hash.count(config.shards[i])){
                  config.shards[i] = 0;
              }
          }
          if(config.groups.empty()){      //说明此时为空，可能是都移出去了，需要清理config的其他成员变量
              config.groups.clear();
              config.shards.resize(NShards, 0);
              return;
          }
          balanceWorkLoad(config);
          configs.push_back(config);
          return;
      }
      if(operation.op == "Move"){         //Move不做负载均衡，那只会破坏Move的语义
          vector<int> moveInfo = getShardAndGroupId(operation.args);
          config.shards[moveInfo[0]] = moveInfo[1];
          // printConfig(config);
          configs.push_back(config);
      }
  }
  ```

  - 负载均衡具体实现

  ```c++
  //自己定义的优先队列的两种排序方式
  class mycmpLower{
  public:
      bool operator()(const pair<int, vector<int>>& a, const pair<int, vector<int>>& b){
          return a.second.size() > b.second.size();
      }
  };
  
  class mycmpUpper{
  public:
      bool operator()(const pair<int, vector<int>>& a, const pair<int, vector<int>>& b){
          return a.second.size() < b.second.size();
      }
  };
  
  //太长了typedef一下
  typedef priority_queue<pair<int, vector<int>>, vector<pair<int, vector<int>>>, mycmpLower> lowSizeQueue;
  typedef priority_queue<pair<int, vector<int>>, vector<pair<int, vector<int>>>, mycmpUpper> upSizeQueue;
  
  //更新lower排序方式的优先队列，需要全清再按照当前的负载情况workLoad进行插值，下面的upper类似
  void syncLowerLoadSize(unordered_map<int, vector<int>>& workLoad, lowSizeQueue& lowerLoadSize){
      while(!lowerLoadSize.empty()){
          lowerLoadSize.pop();
      }
      for(const auto& load : workLoad){
          lowerLoadSize.push(load);
      }
  }   
  
  void syncUpperLoadSize(unordered_map<int, vector<int>>& workLoad, upSizeQueue& upperLoadSize){
      while(!upperLoadSize.empty()){
          upperLoadSize.pop();
      }
      for(const auto& load : workLoad){
          upperLoadSize.push(load);
      }
  }
  
  //负载均衡实现，自己写的可能不是很高效，用了两个优先队列不断更新，主要思想就是把负载最大的拿出来给负载最小的，同时更新workLoad
  //再将两个优先队列继续按照workLoad更新，迭代直到满足负载全相等或最大差一
  void ShardMaster::balanceWorkLoad(Config& config){
      lowSizeQueue lowerLoadSize;
      unordered_map<int, vector<int>> workLoad;
      for(const auto& group : config.groups){
          workLoad[group.first] = vector<int>{};          //先记录下总共有哪些gid(注意：leave去除了gid，把对应的shard置0)
      }
      for(int i = 0; i < config.shards.size(); i++){      //先把为0的部分分配给最小的负载，其实相当于就是再处理move和初始化的join(一开始都为0)
          if(config.shards[i] != 0){
              workLoad[config.shards[i]].push_back(i);    //对应gid负责的分片都push_back到value中，用size表示对应gid的负载
          }
      }
      syncLowerLoadSize(workLoad, lowerLoadSize);
      for(int i = 0; i < config.shards.size(); i++){
          if(config.shards[i] == 0){
              auto load = lowerLoadSize.top();     //找负载最小的组
              lowerLoadSize.pop();
              workLoad[load.first].push_back(i);
              load.second.push_back(i);
              lowerLoadSize.push(load);
          }
      }
      //如果没有为0的，就不断找到负载最大给分给最小的即可
      upSizeQueue upperLoadSize;
      syncUpperLoadSize(workLoad, upperLoadSize);
      if(NShards % config.groups.size() == 0){        //根据是否正好取模分为所有gid的shards数量相同或者最大最小只差1两种情况
          while(lowerLoadSize.top().second.size() != upperLoadSize.top().second.size()){
              workLoad[lowerLoadSize.top().first].push_back(upperLoadSize.top().second.back());
              workLoad[upperLoadSize.top().first].pop_back();
              syncLowerLoadSize(workLoad, lowerLoadSize);
              syncUpperLoadSize(workLoad, upperLoadSize);
          }
      }else{
          while(upperLoadSize.top().second.size() - lowerLoadSize.top().second.size() > 1){
              workLoad[lowerLoadSize.top().first].push_back(upperLoadSize.top().second.back());
              workLoad[upperLoadSize.top().first].pop_back();
              syncLowerLoadSize(workLoad, lowerLoadSize);
              syncUpperLoadSize(workLoad, upperLoadSize);
          }
      }
      //传入的是config的引用，按照最终的workLoad更新传入config的shards
      for(const auto& load : workLoad){
          for(const auto& idx : load.second){
              config.shards[idx] = load.first;
          }
      }
  }
  ```

  

### 四、结语

- 整体结构基本类似LAB3，就是实现的功能完全不一样了
- 需要好好考虑负载均衡的细节，正确性首要其次是效率，LAB4B中数据会根据负载均衡情况在groups间迁移
- 还是要多读几遍文档，虽然做LAB4A可以完全不理解最后需要实现怎样的系统，但通读完所有文档后会发现LAB4A的工作是为LAB4B服务的
- 确保LAB3的功能都能很好的实现，你会发现LAB4B的结构单独抽出每个group又是LAB3的换皮模式升级版
- 感叹下raft层写好的代码真有用，这可能就是造轮子的痛苦和快乐吧，哈哈哈