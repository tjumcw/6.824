### 6.824分布式系统C++实现—————LAB2B:日志复制，一致性检查

注：

- LAB2B主要是各个Raft达成共识的过程，通过Start()向Leader添加命令，现在可以随便添加，实际上这个func是LAB3中kvserver向底层Raft层传递客户端请求的入口函数，需要立即返回(主要为了重选server，LAB3再解释)。
- 在LAB2A实现的appendEntries基础上，引入了带log的心跳即日志同步的RPC请求，需要各个follower通过几次心跳(LAB3C后面有优化)快速达到一致，并将一致的日志apply到应用层。
- 同时日志的引入对选举也提出了新的要求，只有持有term最高的日志或是相同term但较长(可相等)的日志者才会被同意成为leader

### 

### 1、新增的applyLoop

- LAB2B需要实现一个applyMsg结构体，用于将Raft层达成共识的日志发送给应用层。在6.824给出的基本参考代码中，其内封装了一个channel，主要实现阻塞接收信息，在C++中没有该对象，我个人采用了信号量来模拟实现，当然这在LAB2B中不需要实现，只需要留出这样一个同上层通信的接口即可。我个人的封装信息如下：

  ```c++
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
  ```



- 实现的applyLogLoop如下，每次向应用层提交都需要更新applyIndex，而commitIndex是通过leader的appendEntriesRPC已达成的共识的日志索引,其中2层while循环的外层是用于测试快照功能时，让Raftserver强行宕机一段时间后上线能再次进入内层提交日志的循环

  ```c++
  void* Raft::applyLogLoop(void* arg){
      Raft* raft = (Raft*)arg;
      while(1){
          while(!raft->dead){
              usleep(10000);
              printf("%d's apply is called, apply is %d, commit is %d\n", raft->m_peerId, raft->m_lastApplied, raft->m_commitIndex);
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
  ```

  

### 2、elect功能的完善

- RequestVoteArgs需要新增的两个成员变量lastLogTerm及lastLogIndex，用于判断发起vote的candidate是否具有最新最全的日志

  ```c++
  class RequestVoteArgs{
  public:
      int term;
      int candidateId;
      int lastLogTerm;
      int lastLogIndex;
  };
  ```



- 严格遵守论文逻辑

  - 对于接收方，若是接收到的

    ```c++
    RequestVoteArgs.lastLogTerm < selfTerm  // reject the vote
    
    RequestVoteArgs.lastLogTerm == selfTerm && RequestVoteArgs.lastLogIndex < self_logs.size()  // reject the vote
    ```

    

  - 即只有接收到的lastLogTerm > selfTerm， 或是lastLogTerm == selfTerm &&  lastLogIndex >= self_logs.size(),才接受投票请求



### 3、append功能的完善

1、nextIndex[]相关内容

- 同样首先是AppendEntriesArgs类的定义如下：

  ```c++
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
  ```

  - 其中m_prevLogTerm是日志在m_prevLogIndex位置处的所记录所发命令所在的Term，重点关注m_prevLogIndex。
  - 每个raft都维护了对所有其他raft实例的nextIndex[]数组，leader会随着心跳不断更新这个数组，表示对各个server要发的下一个日志索引的猜测，用于判断日志同步的进度。在一个raft成为server时所有nextIndex会初始化为1。
  - 始终有m_prevLogIndex = nextIndex[i] - 1, i表示raft所对应的ID号

- m_prevLogTerm及m_prevLogTerm需要满足论文描述的约束，细看论文对照图2进行修改即可，其中日志不对的回退逻辑很关键，主要分为：

  - 对应位置不存在日志(初始为空特殊考虑)
  - 对应m_prevLogIndex位置的Term ！= m_prevLogTerm
  - 满足约束条件

     -> 需要根据上述情况分别进行日志的回退或是日志的截断及日志的复制

  

2、matchIndex相关内容

- matchIndex也是leader维护并不断更新的一个数组，表示各个raft实例实际同步的日志条目的索引。

![image](https://user-images.githubusercontent.com/106053649/175804800-67b850a1-0722-487b-8ab4-ff6611f6cae1.png)

- 注意上图条件：
  - 需要找到1个N，使得满足所有raft中的已匹配的日志条目索引>=N,并且N > leader自己的已提交条目索引commitIndex
  - leader对于N索引的日志条目对应的Term == leader当前的Term
  - -> 设置当前leader的已提交条目 = N
  - 在下个心跳周期将其封装到AppendEntriesArgs发给各个follower，用于更新follower自己的commitIndex
  - 论文对于follower的commitIndex设置规则：If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last new entry)
  - 在applyLogLoop中不断更新m_lastApplied，直至m_lastApplied = commitIndex，并将每个日志条目封装成ApplyMsg发送给应用层
