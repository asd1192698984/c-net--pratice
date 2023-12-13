#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>
#include <assert.h>

#define EPOLL_SIZE 100

#define MAXBUF 10240
// int flag = 0;
int count = 0;

int udp_accept(int listenfd,struct sockaddr_in my_addr){
    int new_sd = -1;
    int ret = 0;
    int reuse = 1;
    char buf[16];
    struct sockaddr_in peer_addr;  //对方的地址
    socklen_t cli_len = sizeof(peer_addr);
    //接收信息
    ret = recvfrom(listenfd, buf, 16, 0, (struct sockaddr *)&peer_addr, &cli_len);
    if (ret < 0) {
		return -1;
    }
    //创建一个新的socket
     if ((new_sd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("child socket");
        exit(1);
    } else {
        printf("%d, parent:%d  new:%d\n",count++, listenfd, new_sd); //1023
    }
     buf[ret]='\0';
     printf("ret: %d, buf: %s from %d\n ", ret, buf,new_sd);
    ret = setsockopt(new_sd, SOL_SOCKET, SO_REUSEADDR, &reuse,sizeof(reuse));
    if (ret) {
        exit(1);
    }

    ret = setsockopt(new_sd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    if (ret) {
        exit(1);
    }
    //绑定本地地址
    ret = bind(new_sd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr));
    if (ret){
        perror("chid bind");
        exit(1);
    } else {
    }
    //设置地址的协议
    peer_addr.sin_family = PF_INET;
    //udp_accept函数并没有真正调用connect函数来建立连接。它使用了connect函数的模拟行为，
    //为UDP套接字设置了一个默认的目的地，以便后续的数据发送操作可以使用简化的send函数。这样做是为了避免每次发送数据时都要指定目的地的地址信息。
    //是的，connect函数的行为会根据套接字的类型来进行区分。在给定的代码中，connect函数的调用是针对UDP套接字（SOCK_DGRAM）进行的
    //  在TCP套接字（SOCK_STREAM）上调用connect函数时，会建立一个持久的连接，连接的两端可以进行双向的数据交换。
    //但是，在UDP套接字上调用connect函数时，实际上并不会建立持久的连接。UDP是无连接的协议，每个数据报都是独立的。
    if (connect(new_sd, (struct sockaddr *) &peer_addr, sizeof(struct sockaddr)) == -1) {
        perror("chid connect");
        exit(1);
    } else {
    }
    return new_sd;
}
void read_data(int fd){
    char recvbuf[MAXBUF + 1];
    int  ret;
    struct sockaddr_in client_addr;
    socklen_t cli_len=sizeof(client_addr);

    bzero(recvbuf, MAXBUF + 1);
  
    ret = recvfrom(fd, recvbuf, MAXBUF, 0, (struct sockaddr *)&client_addr, &cli_len);
    if (ret > 0) {
        printf("read[%d]: %s  from  %d\n", ret, recvbuf, fd);
    } else {
        printf("read err:%s  %d\n", strerror(errno), ret);
    }
}
int main(){
    int listenfd;
    int epfd;
    int ret;
    int opt=1;
    int port=1234;
    struct epoll_event events[EPOLL_SIZE],ev;
    struct sockaddr_in my_addr;
    //先创建listenfd
    if((listenfd=socket(PF_INET,SOCK_DGRAM,0))==-1){
        perror("socket");
        exit(1);
    }
    //设置一些属性，快速重连和多线程监听同一端口
    ret=setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    if(ret){
        exit(1);
    }
    ret=setsockopt(listenfd,SOL_SOCKET,SO_REUSEPORT, &opt, sizeof(opt));
    if(ret){
        exit(1);
    }
    //设置非阻塞
    int flags=fcntl(listenfd,F_GETFL,0);
    flags |= O_NONBLOCK;
	fcntl(listenfd, F_SETFL, flags);
    //设置IP地址
    bzero(&my_addr,sizeof(my_addr));
    my_addr.sin_family=AF_INET;
    my_addr.sin_port = htons(port);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    //调用bind绑定
     if (bind(listenfd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        exit(1);
    } else {
        printf("IP bind OK\n");
    }
    // 创建epoll池
    epfd=epoll_create(1);
    //listenfd加入epoll监听
    ev.events= EPOLLIN | EPOLLET; //边沿触发
    ev.data.fd=listenfd; 
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) < 0) {
        fprintf(stderr, "epoll set insertion error: fd=%dn", listenfd);  //格式化输出错误信息
        return -1;
    } else {
        printf("ep add OK\n");
    }
    //进入epoll循环
    while(1){
        int nread=epoll_wait(epfd,events,10000,-1);
        if (nread== -1) { //出错
            perror("epoll_wait");
            break;
        }
        int i;
        for (i = 0; i < nread; ++i) {
            if (events[i].data.fd == listenfd) { //接受连接
                    int clientfd;
                    while(1){
                        clientfd=udp_accept(listenfd,my_addr);
                        if (clientfd == -1) break; 
                        //将clientfd加入监听
                        ev.events= EPOLLIN; //边沿触发
                        ev.data.fd=clientfd; 
                        if (epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev) < 0) {
                            fprintf(stderr, "epoll set insertion error: fd=%dn", clientfd);  //格式化输出错误信息
                            return -1;
                        } else {
                            printf("ep add OK\n");
                        }
                    }
				}
             else {
                //直接读消息
                read_data(events[i].data.fd);
            }
        }
    }
    close(listenfd);
}
