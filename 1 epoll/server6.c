#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/poll.h>
#include <sys/epoll.h>
#include <errno.h>
#define BUFFER_LENGTH 120
#define POLL_SIZE 128
//用poll实现
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

    //用epoll来实现
	//先创建一个epoll的管理类 这个1这个参数没什么用  通过fd来操作内核 
	int epfd =epoll_create(1);
	//添加一个最初的事件
	struct epoll_event ev,events[POLL_SIZE];
	ev.events=EPOLLIN;
	ev.data.fd=listenfd;
	//将事件添加进epoll
	epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd,&ev);
	unsigned char buff[BUFFER_LENGTH];
	while(1){	
		//-1没事件就一直阻塞 大于0的数就是等待固定的时间结束等待 可以做定时器
		int nready=epoll_wait(epfd,events,POLL_SIZE,-1);
		int i;
		int ret;
		for( i=0;i<nready;i++){
			int clientfd=events[i].data.fd;
			if(listenfd==clientfd){ //需要accept
				struct sockaddr_in client;
				socklen_t len = sizeof(client);
				int connfd;
				if ((connfd = accept(listenfd, (struct sockaddr *)&client, &len)) == -1) {
					printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
					return 0;
				}
				printf("client %d join\n", connfd);
				//将客户端加入poll池
				ev.events=EPOLLIN;
				ev.data.fd=connfd;
				epoll_ctl(epfd,EPOLL_CTL_ADD,connfd,&ev);
			}else{
				ret = recv(clientfd, buff,BUFFER_LENGTH, 0);
				if(ret>0){
					buff[ret]='\n'; //避免脏数据
					printf("recv msg from client: %s\n", buff);
					fflush(stdout);
					
					//高并发时send不能这样做可能会出现send发送数据发送不完的问题 应该将可读的监听关闭 开启可写的监听 然后再分一个if出去判断可写 这样可以多次将send的数据发送完为止
					send(clientfd, buff, ret, 0);
				}else if(ret==0){
					// fds[i].fd=-1;
					ev.events = EPOLLIN;
					ev.data.fd = clientfd;
					epoll_ctl(epfd, EPOLL_CTL_DEL, clientfd, &ev);
					// epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd,&ev);
					close(clientfd);
				}
			}
		}

	}
    //关闭套接字
    close(listenfd);
    return 0;
}