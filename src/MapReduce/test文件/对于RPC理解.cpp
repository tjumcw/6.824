/* 
    1、两个线程的工作函数
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
        client.set_timeout(1000);
        string  tmp = client.call<string>("assignTask").val();
        std::cout<<pthread_self()<<"tmp :"<<tmp<<std::endl;
        cout<<"call end"<<endl;
    }

    2、服务器端被RPC调用的函数
    string assignTask(){
		string str = "rpc call !";
		printf("%s\n", str.c_str());
		int tmp = 1;
		while(1){
			if(tmp == 1) printf("assignTask %d\n", tmp++);
		}
		return str;
	}

    3、服务器端结果
    rpc call !
    assignTask 1

    4、客户端结果(在1秒后打印，设置了超时时间 -> client.set_timeout(1000);)
       因为未取得返回值，所以打印tmp为空
    140503752640256tmp :
    call end
    140503744247552tmp :
    call end

    5、表明同一时刻只能有一个客户端线程通过RPC调用服务器端的同一个函数
       如上例调用assignTask，函数阻塞，所以只打印了一次信息就停在while循环了

    6、RPC调用的函数只要被call了，只要解除阻塞，哪怕客户端已经timeout，服务器也会调用   

    修改后代码:
    增加了第三个工作线程，设置服务器的assignTask函数2秒后退出，
    且第三个工作线程进来先睡眠5秒，超时时间也设置5秒
    string assignTask(){
		string str = "rpc call !";
		printf("%s\n", str.c_str());
		int tmp = 1;
		while(1){
			if(tmp == 3) {
				printf("break\n");
				break;
			}
			printf("assignTask %d\n", tmp++);
			sleep(1);
		}
		return str;
	}

    void* work3(void* arg){
        sleep(5);	
        buttonrpc client;
        client.as_client("127.0.0.1", 5555);
        client.set_timeout(5000);
        string  tmp = client.call<string>("assignTask").val();
        std::cout<<pthread_self()<<"tmp :"<<tmp<<std::endl;
        cout<<"call end"<<endl;
    }

    结果为:
    (1)服务器: 
        rpc call !
        assignTask 1
        assignTask 2
        break
        rpc call !
        assignTask 1
        assignTask 2
        break
        rpc call !
        assignTask 1
        assignTask 2
        break

    (2)客户端:
        139943502702336tmp :139943494309632tmp :

        call end
        call end
        139943485916928tmp :rpc call !
        call end

    可见前两个线程timeout退出，但第三个线程捕捉到了assignTask的返回值
    同时可以看到，assignTask被执行了三次，哪怕前两个线程1秒后即退出。
    可见，被调用的函数只要结束阻塞，会继续执行，哪怕客户端tmieout结束调用。

    若嵌套调用函数，且函数中被调的函数阻塞，那么其他线程就无法调用该函数和
    其内部的子函数，都被阻塞了。同上若函数阻塞结束，则按调用顺序子服务器端执行。
*/