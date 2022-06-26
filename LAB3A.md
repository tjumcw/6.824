### 6.824分布式系统C++实现—————LAB3A:KvRaft基本结构



注：

- 基于raft的高可用性(只要半数存活)，实现key-value的分布式数据存储系统
- 对于单个client，其请求按序排队提交，支持put、get、append请求
- 多客户端并发保证线性一致性

参考：

- 6.824官方文档[6.824 Lab 3: Fault-tolerant Key/Value Service (mit.edu)](http://nil.csail.mit.edu/6.824/2020/labs/lab-kvraft.html)的要求

- 6.824官方给出的go的骨架代码

  

### 一、客户端细节

```c++
class Clerk{
public:
    Clerk(vector<vector<int>>& servers);
    string get(string key);                                 //定义的对于kvServer的get请求
    void put(string key, string value);                     //定义的对于kvServer的put请求
    void append(string key, string value);                  //定义的对于kvServer的append请求
    void putAppend(string key, string value, string op);    //put、append统一处理函数
	//......

private:
    locker m_requestId_lock;	   //其实可以不要锁，因为实验要求客户端按序请求，执行完一个发下一个
    vector<vector<int>> servers;
    int leaderId;                  //暂存的leaderID，不用每次都轮询一遍
    int clientId;                  //独一无二的客户端ID，在server中有一个map<int, int> 存clientID -> 目前最大requestID
    int requestId;                 //只会递增的该客户端的请求ID，保证按序执行
};
```



#### 1、3个RPC发起函数

- get
  - 有返回值，若datebase无对应key为空字符串，否则为对应的value
  - whileLoop中不断重试（发给另外的server）直到取得value
- put、append
  - 无返回值，只要知道是否设置成功即可
  - whileLoop中不断重试（发给另外的server）直到设置成功

- 示例代码(get类似)

```c++
int cur_portId = 0;         //为了减轻server端的RPC压力，所以server对PUT,GET,APPEND操作设置了多个RPC端口响应
locker port_lock;           //由于applyLoop中做了处理，只响应递增请求，满足线性一致性，且applyLoop是做完一个在做下一个
                            //即完全按照raft日志提交顺序做，客户端并发虽然不能判断哪个先写入日志，但能保证看到的一定是满足按照日志应用的结果

void Clerk::putAppend(string key, string value, string op){
    PutAppendArgs args;
    args.key = key;
    args.value = value;
    args.op = op;
    args.clientId = clientId;
    args.requestId = getCurRequestId();			//函数体内取完requestID会加1，保证增序且唯一
    int cur_leader = getCurLeader();

    port_lock.lock();
    int curPort = (cur_portId++) % EVERY_SERVER_PORT;	//使用的RPC框架是阻塞式，任务排队完成，所以server端使用多个port监听同样请求
    port_lock.unlock();

    while(1){
        buttonrpc client;					//创建rpc客户端对象
        client.as_client("127.0.0.1", servers[cur_leader][curPort]);	//绑定server监听请求的其中一个端口
        PutAppendReply reply = client.call<PutAppendReply>("putAppend", args).val();    //取得RPCreply，对于put、append只需知道是否成功，直到成功才停止
        if(!reply.isWrongLeader){
            return;
        }
        printf("clerk%d's leader %d is wrong\n", clientId, cur_leader);
        cur_leader = getChangeLeader();
        usleep(1000);
    }   
}
```

#### 2、注意要点

- 每个客户端启动后随机分配独一无二的ID，其每个请求由单调递增的requestID来标识
- buttonrpc client需要定义在while内，不然无法退出循环(不知道为什么，gdb单步调试试出来的)



### 二、server端细节

```c++
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

    //string getSnapShot();                       //将kvServer的状态信息转化为snapShot
    //void recoverySnapShot(string snapShot);     //将从raft层获得的快照安装到kvServer即应用层中(必然已经落后其他的server了，或者是初始化)

    //---------------------------test----------------------------
    //bool getRaftState();            //获取raft状态
    //void killRaft();                //测试安装快照功能时使用，让raft暂停接受日志
    //void activateRaft();            //重新激活raft的功能

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
```



#### 1、3个map

- m_database ： 模拟数据库，用来存放客户端请求产生的数据
- m_clientSeqMap ： 存储了每个客户端当前最大请求ID，用来保证按序执行和重复检测
- m_requestMap ： 记录server端的RPChandler中的上下文信息，参考了[MIT 6.824: Distributed Systems- 实现Raft Lab3A | 鱼儿的博客 (yuerblog.cc)](https://yuerblog.cc/2020/08/19/mit-6-824-distributed-systems-实现raft-lab3a/)的实现

```c++
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
```



#### 2、2个RPC处理函数

- 关键处理：

  - 提供的go骨架代码给出了提示，监听OpContext结构体中定义的channel收到的数据，若超时没收到重试（需要实现C++类似功能）

  - 首先需要实现raft层向kvServer层提交msg的channel，具体来说

    - raft层向应用层kvServer通过applymsg中的channel发送数据
    - kvServer中的applyLoop中不断监听channel，取到一个处理一个
    - 处理完后通过OpContext保存的上下文信息唤醒对应的管道

  - C++中我的实现

    - 在raft类的实现中定义了一个当前需应用msg队列，并利用send即recv两个信号量模拟了这一raft向kvServer提交msg的生产者消费者问题

    - ```c++
      class Raft{
      public:
      	void setSendSem(int num);           //初始化send的信号量，结合kvServer层的有名管道fifo模拟go的select及channel
          void setRecvSem(int num);           //初始化recv的信号量，结合kvServer层的有名管道fifo模拟go的select及channel
          bool waitSendSem();                 //信号量函数封装，用于类复合时kvServer的类外调用
          bool waitRecvSem();                 //信号量函数封装，用于类复合时kvServer的类外调用
          bool postSendSem();                 //信号量函数封装，用于类复合时kvServer的类外调用
          bool postRecvSem();                 //信号量函数封装，用于类复合时kvServer的类外调用
          ApplyMsg getBackMsg();              //取得一个msg，结合信号量和fifo模拟go的select及channel，每次只取一个，处理完再取
      
      private:
              //用作与kvRaft交互
          sem m_recvSem;                      //结合kvServer层的有名管道fifo模拟go的select及channel
          sem m_sendSem;                      //结合kvServer层的有名管道fifo模拟go的select及channel
          vector<ApplyMsg> m_msgs;            //在applyLogLoop中存msg的容易，每次存一条，处理完再存一条
      };
      ```

    - 以上只写出相关部分，raft层在提交msg的Loop中需要结合两个信号量处理，类似如下

    - ```c++
      for(int i = 0; i < msgs.size(); i++){
          raft->waitRecvSem();
          raft->m_msgs.push_back(msgs[i]);
          raft->postSendSem();
      }
      ```

    - 而在对应kvServer层需要有类似处理

    - ```c++
      void* KVServer::applyLoop(void* arg){
          KVServer* kv = (KVServer*)arg;
          while(1){
              kv->m_raft.waitSendSem();
              ApplyMsg msg = kv->m_raft.getBackMsg();
              /*
              *处理逻辑
              */
              kv->m_raft.postRecvSem();
          }
      }
      ```

  - 其实需要实现OpContext结构体中定义的channel与applyLoop交互，即要么applyLoop将得到的OpContext中的channel唤醒，要么超时重发

  - C++中我的实现

    - 通过有名管道fifo及自己写的监听fifo的select类实现

    - ```c++
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
      ```

    - 在applyLoop中若能通过msg中的index信息在map中取得对应的OpContext，则向对应fifo发送消息触发select

    - ```c++
      //保证只有存了上下文信息的leader才能唤醒管道，回应clerk的RPC请求(leader需要多做的工作)
      if(isOpExist){  
          int fd = open(opctx->fifoName.c_str(), O_WRONLY);
          char* buf = "12345";
          write(fd, buf, strlen(buf) + 1);
          close(fd);
      }    
      ```

    - 对应get及putAppend的RPC处理函数等待触发，超时返回err给client，client重发RPC,对应处理看下文的RPC处理函数示例



- RPC处理函数：get、put、append(均类似)

```c++
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
```



### 三、结语

- 剩下的就是写好applyLoop中关于判断请求是否应该处理以及如何处理
- 注意kvServer层与raft层不要产生死锁
- 信号量注意要在锁外wait()，加锁后再wait()容易出问题