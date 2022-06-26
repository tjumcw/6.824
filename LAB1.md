### 6.824分布式系统C++实现—————LAB1:MapReduce

注：听说很锻炼能力，就尝试去做，但课程官网从有这门课开始就一直是基于go实现的LAB，想去找对应的测试脚本翻了翻网上没有一个C++版本。没办法，只能硬上，只能针对各种情况进行手动设计bug进行测试。而且C++实现起来相对较难，尤其在LAB2的Raft实现中麻烦了不少，实名羡慕goroutine，而且没有官方提供go实现的每个LAB最最基本的框架，完成LAB2后就准备记录一下自己的学习及code过程，希望能帮到大家，祝大家LAB顺利！

### 1、前期准备

1、需要阅读的论文：[rfeet.qrk (mit.edu)](http://nil.csail.mit.edu/6.824/2020/papers/mapreduce.pdf)

2、需要git的仓库：git clone git://g.csail.mit.edu/6.824-golabs-2020 6.824

- LAB官方基于go实现，但代码一些注释的细节，整体框架以及测试情况需要了解，还是要download仔细阅读

3、环境配置

- 如果你用的go，直接按照[6.824 Lab 1: MapReduce (mit.edu)](http://nil.csail.mit.edu/6.824/2020/labs/lab-mr.html)的LAB1引导来做，如果和我一样C++选手，需要额外配置一下非STL的库（用于RPC），我个人没选择C库里 <rpc/rpc.h>下的RPC实现，感觉不是很方便。这里推荐下[github.com](https://github.com/button-chen/buttonrpc)的一个轻量级RPC库，需要安装zeromq和cppzmq的依赖，具体见[zeromq/cppzmq: Header-only C++ binding for libzmq (github.com)](https://github.com/zeromq/cppzmq)最底部的引导



### 2、MapReduce基本框架

![image](https://user-images.githubusercontent.com/106053649/171107357-7042f8c9-a8cd-4643-a956-d71904add50f.png)

基本原理

- MapReduce是一种编程模型，用于大规模数据集的并行运算。在分布式系统的背景下，首先需要明确各个执行Map任务和Reduce任务的计算机是不知道彼此的存在，他们只负责处理给予的任务并写入文件系统，也无法确保任务是否能顺利完成，故除了执行任务的worker以外，需要一个协调所有worker的master。
- master需要分配所有的任务，map阶段将需要处理的文件分配给执行map的worker，reduce阶段类似，在LAB1中是分配reduce任务编号。master还需实现状态的确认以及超时任务的重做（需要有定时任务处理），以确保任务阶段的变化以及失败任务的重新分发及处理。
- worker需要加载map及reduce函数，由于工作性质的不同，所以需要运行时动态加载，C/C++可以利用dlopen及dlsym来实现。具体的map及reduce工作，包括shuffle过程，建议看论文及视频，多思考思考。稍微提下一个容易想不明白的地方：执行recude的worker怎么知道他需要处理哪些文件呢？
  - LAB1的基本代码中有一个ihash函数，简而言之就是将map处理结果对recude总数取模，反映在lab1中可以体现为对所有words求和取模，后续拿到recude编号的worker读取对应尾缀的文件即可。
- 推荐一个介绍MapReduce基本原理的视频，[深入浅出讲解 MapReduce_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1Vb411m7go?spm_id_from=333.851.header_right.fav_list.click)可以在看完论文和6.824官方视频后加深理解。



### 3、RPC

RPC是远程过程调用（Remote Procedure Call）的缩写形式，在分布式系统中无处不见，可用于server与server，server与client间的通信，它隐藏了网络通信的具体实现细节，只留出调用的接口，便利了一台主机调用另一台主机上的函数/方法这一过程。

- 在MapReduce中，worker需要通过RPC-request向master申请任务。因为worker之间互相不可知，worker只负责处理交给自己的任务，而master拥有所有的任务信息和状态信息，所以需要worker远程调用master的assignTask()方法，通过返回值取得任务。同理worker完成任务后，通过setState()方法远程更新状态信息。
- 如果使用我推荐的RPC库，有一些细节能帮助你快速熟悉使用方法，同时建议看库的example程序
  - 在调用server.run()后，server进程位于while死循环中不断接受client的request并处理，即server进程阻塞于此处。
  - 由于master类中有许多共享数据，处理时需要加锁，但请记住不要在主线程的RPC方法中使用易产生阻塞的同步原语如信号量，虽然可以设置RPC方法调用的超时时间，但这导致后续所有调用被加入队列等待阻塞结束/超时后继续执行RPC请求，若有必要在其他线程中处理同步。
  - 模板中没有实现一些容器的序列化和反序列化，返回值可以是基本数据类型以及封装这些数据类型的类对象，若在类中有string成员变量，请仔细看库里的example，类外string无需考虑。



### 4、定时任务

针对master需要实现任务的超时重分配，需要实现简单的定时任务，C++中常用的简单的定时方式有

- 1、在当前进程中再开一个线程进行定时，并以pthread_join()方式回收定时子线程，并checkState检查任务状态。
  - 下面这段代码实现了一个简单定时，类A分配任务后创建一个work线程并detach不影响主线程继续分配任务，在work线程中创建wait线程睡眠指定时间并以join方式回收，当定时结束时work线程结束阻塞，进行超时判断并分别进行不同逻辑处理。

```c++
//举个简单的例子，为了逻辑清晰，我就按照定时逻辑顺序写
class A{
public:
    static void* wait(void* arg);//定时wait一定时间后被work线程回收
    static void* work(void* arg);//工作线程的回调函数，回收wait线程结束阻塞执行后续逻辑
    string assignTask();//
    void checkState();
    void waitTask();
	//...
private:
    //...
};

string A::assignTask(){
    //完任务
    waitTask();
}
void A::waitTask(){
    pthread_t tid;
    //需要额外一个线程来回收定时线程,不然会阻塞,传this是因为定时结束后需要调用checkState()确认状态
    pthread_create(&tid, NULL, work, this);   
    pthread_detach(tid);
}
void* A::work(void* arg){
    A* a = (A*)arg;
    pthread_t tid;
    pthread_create(&tid, NULL, wait, NULL);
    pthread_join(tid);   //这里需要回收，知道定时结束才会结束阻塞执行判断的逻辑
    a->checkState();     //判断是否超时分别进行不同的逻辑处理
    //业务逻辑
}
void* A::wait(void* arg){
    sleep(5);      //睡眠5秒
}
```

- 2、使用条件变量pthread_cond_timedwait()进行定时
  - 考虑到上述方法在执行时必须等到超时才不阻塞，若是同时巨量任务执行会有大量定时线程阻塞等待超时，考虑到开销用条件变量定时，条件满足或超时都会结束阻塞
  - 写一个简单的示例，理解了可以直接应用到master类中。例子中实现了条件变量定时，若超时或者调用signal(),broadcast()会结束阻塞并调用传入的回调函数func()。

```c++
pthread_cond_t m_cond;
pthread_mutex_t mutex_cond;

void* work(void* arg){
    pthread_mutex_lock(&mutex_cond);
    pthread_cond_signal(&m_cond)
    pthread_mutex_unlock(&mutex_cond);
}

void func(){
    cout<<"1"<<endl;
}

void setTimeCond(void (*func)()){
    while(1){
        pthread_mutex_init(&mutex_cond, NULL);
        pthread_cond_init(&m_cond, NULL);
        pthread_t tid;
        pthread_create(&tid, NULL, work ,NULL);
        pthread_mutex_lock(&mutex_cond);
        cout<<"等待"<<endl;
        struct timespec abs_timeout;
		abs_timeout.tv_sec = time(NULL) + 5;
		abs_timeout.tv_nsec = 0;   //若要实现ms级定时，需要考虑tv_nsec > 1000000000的情况
        						   //tv_sec += 1; tv_nsec -= 1000000000;
        pthread_cond_timedwait(&m_cond, &mutex_cond, &abs_timeout);//5秒后或者被唤醒不阻塞
        func();   //调用传入的函数指针
        pthread_mutex_unlock(&mutex_cond);
    }
}

int main(){
    setTimeCond(func);
}
```

### 5、一些细节

1. worker中始终用到的class

   ```c++
   class KeyValue{
   public:
       string key;
       string value;
   };
   ```

   - 传入mapFunc的keyValue中key指filename， value指文件的content
   - map结束后写入中间文件的key指word，value都为“1”
   - 传入reduceFunc的keyValue中key指word，value指“11111...”，即有几个word有几个1
   - reduce结束后写入输出文件的格式为key:value,key为word，value为len(word)即上一条中1的个数

2. 动态加载mapFunc及reduceFunc

   ```c++
   //map_reduceFun.cpp
   extern "C" vector<KeyValue> mapF(KeyValue kv){
       //......
   }	
   extern "C" vector<string> reduceF(vector<KeyValue> kvs, int reduceTaskIdx){
   	//......
   }
   ```

   对动态库编译makefile文件如下

   ```makefile
   libmrFunc.so:map_reduceFun.o
   	@g++ -shared map_reduceFun.o -o libmrFunc.so
   map_reduceFun.o:map_reduceFun.cpp
   	@g++ -fpic -c map_reduceFun.cpp
   
   .PHONY:clean
   clean:
   	rm *.o *.so
   ```

   - 需要包含<dlfcn.h>头文件，调用dlopen()及dlsym进行map和reduce函数的动态加载

3. master类

   - 简单提供下思路以及C++中实现起来不太方便的部分，最后附一个我个人的master类设计，如果实在没想法可以参考，就不给注释啦，看名字就知道大概干嘛的了

   ```c++
   class Master{
   public:
       static void* waitMapTask(void *arg);
       static void* waitReduceTask(void* arg);
       static void* waitTime(void* arg);
       Master(int mapNum = 8, int reduceNum = 8);
       void GetAllFile(char* file[], int index);
       int getMapNum(){
           return m_mapNum;
       }
       int getReduceNum(){
           return m_reduceNum;
       }
       string assignTask();
       int assignReduceTask();
       void setMapStat(string filename);
       bool isMapDone();
       void setReduceStat(int taskIndex);
       void waitMap(string filename);
       void waitReduce(int reduceIdx);
       bool Done();
       bool getFinalStat(){
           return m_done;
       }
   private:
       bool m_done;
       list<char *> m_list;
       locker m_assign_lock;
       int fileNum;
       int m_mapNum;
       int m_reduceNum;
       unordered_map<string, int> finishedMapTask;
       unordered_map<int, int> finishedReduceTask;
       vector<int> reduceIndex;
       vector<string> runningMapWork;
       int curMapIndex;
       int curReduceIndex;
       vector<int> runningReduceWork;
   };
   
   ```

   
