#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#define BUFFER_LENGTH 120
int main(){
    printf("hello\n");
    int listenfd=socket(AF_INET,SOCK_STREAM,0);
    //解决bind绑定再次重启报错 要用ctrl+c结束
    int reuse=1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    if(listenfd==-1) {
        printf("-1");
        return -1;
    } 
    
    struct sockaddr_in servaddr;
    servaddr.sin_family=AF_INET;
    //绑定的是0.0.0.0  htonl将主机字节顺序改成网络字节顺序
    //IP
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    // 端口
    servaddr.sin_port=htons(9995); 
    //bind 绑定socket和本地端口
    if(-1==bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr))){
        printf("-2");
        return -2;
    }


    //请求队列最大长度 10 listen 设置监听的设置
    printf("listen\n");
    listen(listenfd,10);


    //用IO复用来做
    fd_set rfds,wfds,origin_rfd,origin_wfd;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_SET(listenfd,&origin_rfd);
    int maxfd=listenfd;
    int ret; //收到信息的字数
    unsigned char buffer[BUFFER_LENGTH]={0}; //收发缓冲区
    printf("进入循环\n");
    while(1){
        rfds=origin_rfd; //copy一份最初的集合
        wfds=origin_wfd;
        //等待文件描述符就绪
        //在这里虽然一开始将listenfd设置在了origin_rfd中但是并不意味着select会直接返回
        //select监听文件描述符集合中的所有文件状态，当文件状态满足时才停止阻塞
        printf("select阻塞\n");
        fflush(stdout);//这可能是因为标准输出（stdout）是行缓冲的，而 select 可能一直在等待文件描述符就绪，导致输出没有立即刷新到屏幕上。
        int nready=select(maxfd+1,&rfds,&wfds,NULL,NULL);  
        if(FD_ISSET(listenfd,&rfds)){
            printf("start listen\n"); //在没有accept的时候会一直打印 应该是accept之后会将可读的状态关闭
            struct sockaddr_in client;
            //客户端结构体长度
            socklen_t len=sizeof(client);
            // 这里阻塞进行监听
            int clientfd=accept(listenfd,(struct sockaddr*)&client,&len);
            printf("clientfd :%d join\n",clientfd);
            //然后将客户端也加入可读的事件监听
            FD_SET(clientfd,&origin_rfd); 
            if(clientfd>maxfd) maxfd=clientfd;
        }
        //循环处理IO
        int i=0;
        for(i=listenfd+1;i<=maxfd;i++){ //对所有的客户端进行判断 因为client的连接应该都是默认打开可读可写

            if(FD_ISSET(i,&rfds)){//判断可读
                printf("等待client%d的数据\n",i);
                ret=recv(i,buffer,BUFFER_LENGTH,0);  //阻塞函数
                if(ret>0){//连接正常关闭，远程端关闭了连接。recv返回0
                    printf("comefrom client%dbuffer:%s \nret :%d\n",i,buffer,ret);
                    FD_SET(i,&origin_wfd);//设置监测可写的事件 但是下次循环生效
                    FD_CLR(i,&origin_rfd);//取消监测可读的事件 但是下次循环生效
                }
                else if(ret==0){ //为了解决客户端断开不断返回0的情况
                    printf("client%d quit\n",i);
                    fflush(stdout);
                    close(i);
                    FD_CLR(i,&origin_rfd);//取消监测可读的事件 
                    FD_CLR(i,&origin_wfd);//取消监测可写的事件 
                    break;
                }else{
                    
                }
                // send(clientfd,buffer,ret,0);
            }
            if(FD_ISSET(i,&wfds)){
                send(i,buffer,ret,0);//发送上次读到的数据
                FD_SET(i,&origin_rfd);//设置监测可读的事件 但是下次循环生效
                FD_CLR(i,&origin_wfd);//取消监测可写的事件 但是下次循环生效
            }
        }
    }
    //关闭套接字
    close(listenfd);
    return 0;
}