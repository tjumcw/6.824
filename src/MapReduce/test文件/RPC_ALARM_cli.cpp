/**
 * @file RPC_ALARM_cli.cpp
 * @author your name (you@domain.com)
 * @brief  测试多个线程同时调用一个服务端的RPC阻塞业务时，调用情况会如何
 * @version 0.1
 * @date 2022-05-24
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <string>
#include <iostream>
#include <ctime>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include "./buttonrpc-master/buttonrpc.hpp"


bool flag = false;
void myalarm(int num) {
	flag = true;
	printf("called\n");
}

void* work(void* arg){
	struct sigaction act;
    act.sa_flags = 0;
    act.sa_handler = myalarm;
    sigemptyset(&act.sa_mask);  // 清空临时阻塞信号集
   
    // 注册信号捕捉
    sigaction(SIGALRM, &act, NULL);

    struct itimerval new_value;

    // 设置间隔的时间
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_usec = 0;

    // 设置延迟的时间,2秒之后开始第一次定时
    new_value.it_value.tv_sec = 2;
    new_value.it_value.tv_usec = 0;
	setitimer(ITIMER_REAL, &new_value, NULL); // 非阻塞的

	buttonrpc client;
	client.as_client("127.0.0.1", 5555);
	client.set_timeout(5000);
	int num = client.call<int>("getNum").val();
	bool ret = client.call<bool>("Yes", 11).val();
	//string str = client.call<string>("get", "hello world !!!").val();
	//string  tmp = client.call<string>("assignTask").val();
	//char* buf= client.call<char*>("ch", "get char*").val();
	std::cout <<pthread_self()<<"num :"<<num <<std::endl;
	std::cout <<pthread_self()<<"ret :"<<ret <<std::endl;
	//std::cout <<pthread_self()<<"str :"<<str <<std::endl;
	//std::cout<<pthread_self()<<"tmp :"<<tmp<<std::endl;
	//std::cout <<pthread_self()<<"ch* :"<<buf <<std::endl;
	while(!flag);
	flag = false;
}

void* work1(void* arg){
	buttonrpc client;
	client.as_client("127.0.0.1", 5555);
	client.set_timeout(1000);
	string  tmp = client.call<string>("assignTask").val();
	std::cout<<pthread_self()<<"tmp :"<<tmp<<std::endl;
	cout<<"call end"<<endl;
}

void* work2(void* arg){
	buttonrpc client;
	client.as_client("127.0.0.1", 5555);
	client.set_timeout(5000);
	string  tmp = client.call<string>("assignTask").val();
	std::cout<<pthread_self()<<"tmp :"<<tmp<<std::endl;
	cout<<"call end"<<endl;
}

void* work3(void* arg){
	sleep(5);	
	buttonrpc client;
	client.as_client("127.0.0.1", 5555);
	client.set_timeout(5000);
	string  tmp = client.call<string>("get", "Hello").val();
	std::cout<<pthread_self()<<"tmp :"<<tmp<<std::endl;
	cout<<"call end"<<endl;
}

int main() {
	buttonrpc client;
	client.as_client("127.0.0.1", 5555);
	client.set_timeout(5000);
	// int num = client.call<int>("getNum").val();
	// std::cout <<getpid()<<"num :"<<num <<std::endl;
	// pthread_t tid1;
	// pthread_t tid2;
	// pthread_t tid3;
	// pthread_create(&tid1, NULL, work1, NULL);
	// pthread_detach(tid1);
	// pthread_create(&tid2, NULL, work2, NULL);
	// pthread_detach(tid2);
	// pthread_create(&tid3, NULL, work3, NULL);
	// pthread_detach(tid3);
	pthread_t tid[5];
	int * thread_retval[5];
	for(int i = 0; i < 5; i++){	
		pthread_create(&tid[i], NULL, work, NULL);
		pthread_join(tid[i], (void **)&thread_retval[i]);
	}
	return 0;
}