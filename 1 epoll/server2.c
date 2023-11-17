#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#define BUFFER_LENGTH 120

//用多线程让server接受多个连接

void* routine(void * arg){
    int clientfd =*(int*)arg;
    while(1){
        unsigned char buffer[BUFFER_LENGTH]={0};
        int ret=recv(clientfd,buffer,BUFFER_LENGTH,0);
        if(ret==0){ //为了解决客户端断开不断返回0的情况
            close(clientfd);
            break;
        }
        printf("buffer:%s\n ret :%d\n",buffer,ret);
        send(clientfd,buffer,ret,0);
    }
    return 0;
}
int main(){
    printf("hello\n");
    int listenfd=socket(AF_INET,SOCK_STREAM,0);
    if(listenfd==-1) return -1; 
    
    struct sockaddr_in servaddr;
    servaddr.sin_family=AF_INET;
    //绑定的是0.0.0.0  htonl将主机字节顺序改成网络字节顺序
    //IP
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    // 端口
    servaddr.sin_port=htons(9999);
    //bind 绑定socket和本地端口
    if(-1==bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr))){
        return -2;
    }

    //改成非阻塞型
#if 0
    int flag =fcntl(listenfd,F_GETFL,0);
    flag |=O_NONBLOCK;
    fcntl(listenfd,F_SETFL,flag);
#endif

    //请求队列最大长度 10 listen 设置监听的设置
    listen(listenfd,10);

    while(1){
         printf("11");
        //客户端地址
        struct sockaddr_in client;
        //客户端结构体长度
        socklen_t len=sizeof(client);
        // 这里阻塞进行监听
        int clientfd=accept(listenfd,(struct sockaddr*)&client,&len);
        printf("clientfd :%d\n",clientfd);
        //创建线程
        pthread_t id;
        pthread_create(&id,NULL,routine,&clientfd);
    }
    return 0;
}