#include "shardClerk.hpp"
using namespace std;



int main(){
    srand((unsigned)time(NULL));
    vector<vector<int>> port = getMastersPort(EVERY_SERVER_RAFT);
    printf("new test : size is %d\n", port.size());
    // printf("server.size() = %d\n", port.size());
    shardClerk* clerk = new shardClerk[EVERY_SERVER_RAFT];

    for(int i = 0; i < EVERY_SERVER_RAFT; i++){
        clerk[i].makeClerk(port);
    }

    //首先拉取最新配置，需要注意的是，通过持久化保存日志，
    //在raft的applyLogLoop中会从0开始重演日志到server的状态机中
    //所以测试时server和client都重新启动，server的applyLoop会读取日志并调用
    //doJoinLeaveMove()快速重建状态机，所以client的configNum会不是0，实际上
    //server重新运行时是0，但很快就重建完了，实际运行时不可能像这样同时启动关闭
    //server和client，server是无时无刻运行着的，而且也只需要通过query(-1)拉取
    //最新配置即可，这样也保证了所有客户端的历史记录都可见，很合理。真要通过configNum
    //查询只需先query(-1)拉取最新配置，在读取config.configNum得到当前号，对自己做了几个
    //操作，在查对应的index即可。也必须首先获取最新配置信息，才能根据当前分片情况进行调整
    
    Config config = clerk[3].Query(-1);   
    printConfig(config);

    while(1){
        unordered_map<int, vector<string>> servers;
        servers[1] = vector<string>{"ab", "bc", "cd"};
        servers[2] = vector<string>{"de", "ef", "fg"};
        clerk[3].Join(servers);
        config = clerk[3].Query(-1);
        printConfig(config);

        unordered_map<int, vector<string>> server1;
        server1[3] = vector<string>{"m"};
        clerk[3].Join(server1);
        config = clerk[4].Query(-1);
        printConfig(config);    

        clerk[0].Move(0, 3);
        clerk[2].Move(1, 3);
        clerk[3].Move(2, 3);
        config = clerk[1].Query(-1);
        printConfig(config); 

        vector<int> gid{2};
        clerk[0].Leave(gid);
        config = clerk[3].Query(6);
        printConfig(config);

        unordered_map<int, vector<string>> server2;
        server2[2] = vector<string>{"q"};
        clerk[4].Join(server2);
        config = clerk[1].Query(-1);
        printConfig(config);

        config = clerk[1].Query(33);
        printConfig(config);
    }
}
