### 6.824分布式系统C++实现—————LAB2C:持久化，日志快速回退

注：在LAB2C中需要对一些状态持久化，用以在恢复启动时重建状态机。同时需要对不匹配日志进行快速回退，虽然在论文中觉得这样的情况几乎不会出现，但实现该功能使得Raft更完善。



### 1、持久化状态

```c++
class Persister{
public:
    vector<LogEntry> logs;
    string snapShot;		//后续在LAB3中进行快照时需要持久化的状态
    int cur_term;
    int votedFor;
    int lastIncludedIndex;	//后续在LAB3中进行快照时需要持久化的状态
    int lastIncludedTerm;	//后续在LAB3中进行快照时需要持久化的状态
};
```

- 实际要求在上述这些持久化状态变化时，需要进行持久化，具体实现可以在RPC发起请求及回复时，若发生状态变化则持久化(开销较大)
- 需要实现的即saveState()及readState()两个函数，分别进行状态的保存以及宕机恢复时的状态重建
- 个人的C++实现与6.824go给的持久化代码不同，我直接通过Linux下的write()写在磁盘上，所以写成string形式(char*),只要自己设计协议写入和读取匹配即可



### 2、日志快速匹配

- 建议仔细阅读6.824的学生指导[Students' Guide to Raft :: Jon Gjengset (thesquareplanet.com)](https://thesquareplanet.com/blog/students-guide-to-raft/#an-aside-on-optimizations)，看明白就做完了

- 在AppendEntriesReply类中维护了两个新的变量用于冲突时日志的快速回退

  ```c++
  class AppendEntriesReply{
  public:
      int m_term;
      bool m_success;
      int m_conflict_term;
      int m_conflict_index;
  };
  ```

- 一些小的tips：
  - write()接受const  void *buf，此处string需要转化为const char *，可以通过c_str()
  - 写入时长度需要设置为string.size() + 1，为字符串结束符，不然读取时有时候最后一位会有乱码，虽然也不影响实际效果(在协议设置好的情况下)



结：主要就是对照论文进行持久化功能的实现，对照学生指导进行日志快速匹配功能的实现。整个LAB2主要就是对着论文的figure2进行代码编写，当然在后续LAB3中当实现日志压缩即快照功能，基本所有的模块都需要大改，而且调试bug会很痛苦，至少先把LAB2给它好好实现了，祝码的愉快，加油！！！