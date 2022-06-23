#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <bits/stdc++.h>
#include <unistd.h>
using namespace std;



void* read(void* arg){
    sleep(1);
    char* buf = "12345";
    int fd = open("test", O_WRONLY);
    write(fd, buf, strlen(buf));
    printf("first write finished\n");
}

void* write(void* arg){
    char buf[1024];
    printf("???????????\n");
    int fd = open("test", O_RDONLY);
    int len = read(fd, buf, sizeof(buf));
    printf("%s\n", buf);
}

int main(){
    int ret = mkfifo("test", 0664);
    pthread_t tid;  
    pthread_create(&tid, NULL, read, NULL);
    pthread_detach(tid);
    pthread_t tid2;
    pthread_create(&tid2, NULL, write, NULL);
    pthread_detach(tid2);
    while(1);
}