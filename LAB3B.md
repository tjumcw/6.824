### 6.824分布式系统C++实现—————LAB3A:KvRaft日志压缩

注：

- 首先思考日志和快照的区别
  - 日志记录了当前应用层所有执行的请求，通过重演日志可以将一个空的状态机恢复到当前状态
  - 快照是存储了某一checkpoint时的状态信息，相当于某一时刻应用层的所有数据状态
  - 本质上是一个东西，考虑到生活中读请求比写请求多了很多倍，日志肯定有很多来回覆盖数据的地方
  - 通过实现快照，可以截断快照的checkpoint前的所有日志，进行日志压缩
  - 新快照可以直接覆盖旧快照
- 简要步骤：
  - raft层通过应用层调start()接受日志，得到共识后提交到应用层改变应用层状态，一段时间后日志超过一定大小则进行快照
  - 快照在应用层进行，记录当前应用层状态，并发给raft层
  - raft层接受快照，并进行持久化，保存快照
  - 需要安装快照的两种情况：
    - 节点重新启动时，读取持久化的快照，并向应用层传递快照在应用层恢复状态
    - 某个节点的日志信息落后于leader的快照状态时，由leader在appendEntriesLoop中进入installSnapShot分支，向应用层安装快照

- 需要修改的地方(相当之多，调试起来巨头疼)
  - 接受快照截断日志之后，所有涉及日志的长度以及索引的地方，都需要重新进行设计和判断
  - 很多逻辑也需要重新处理，因为多了快照，需要对日志索引和快照对应的lastLogIndex进行比较
  - 需要重新设计按照快照的RPC发起函数以及RPC处理函数
  - 持久化参数也变多了，需要记录快照对应到日志的index以及term

- 可以看的资料
  - [6.824 Lab 3: Fault-tolerant Key/Value Service (mit.edu)](http://nil.csail.mit.edu/6.824/2020/labs/lab-kvraft.html)官方文档
  - raft论文Section7
  - ![image](https://user-images.githubusercontent.com/106053649/175804871-a8459b9e-7cf2-4011-99b6-3242c9d369ba.png)
  - 对照上图进行RPC设计，其中offset以及done参数暂且不考虑
- kvServer需要做的工作
  - 取得快照的函数实现
  - 安装快照的函数实现
  - 一个持续检测raft层日志长度的守护线程
  - 向raft层发送快照的函数
  - applyLoop中增加快照处理的分支

- 差不多就是这些了，其实流程设计起来很简单，但是修改逻辑很麻烦，raft本来写的够长了，需要重新考虑下标并修改逻辑，难度不大但很琐碎。就简单贴一下kvServer层的简单快照框架

  - 持续监听raft日志长度的守护线程

    ```c++
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
    ```

    

  - 根据msg.commandValid判断是否是快照请求

    ```c++
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
                /*
                * 处理请求的逻辑
                */
            }
            kv->m_raft.postRecvSem();
        }
    }
    ```

    

- 需要耐下性子，好好修改raft层，慢慢来
