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
#define POLL_SIZE 10
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

    //用poll来实现 先定义一个句柄的的数组
	struct pollfd fds[POLL_SIZE]={0};
	//用第一个fd来当作服务器的句柄
	fds[0].fd=listenfd;
	fds[0].events=POLLIN;

	int maxfd=listenfd;
	//初始化变量
	int i=0;
	//缓冲池
	unsigned char buff[BUFFER_LENGTH];
	int ret;
	//置为-1 poll函数不会处理
	for(i=1;i<POLL_SIZE;i++) fds[i].fd=-1;
	while(1){
		//-1为无限等待
		printf("wait poll\n");
		fflush(stdout);
		int nready=poll(fds,maxfd+1,-1);
		printf("end wait poll\n");
		fflush(stdout);
		if(fds[0].revents&POLLIN){  //listen触发了accept
			struct sockaddr_in client;
		    socklen_t len = sizeof(client);
			int connfd;
		    if ((connfd = accept(listenfd, (struct sockaddr *)&client, &len)) == -1) {
		        printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
		        return 0;
		    }
			printf("client %d join\n", connfd);
			//将客户端加入poll池
			fds[connfd].fd=connfd;
			fds[connfd].events=POLLIN;
			if(connfd>maxfd) maxfd=connfd;

			//这里可以判断处理的次数 处理完了直接进入下一次poll
			if (--nready == 0) continue;
		}
		for(i=listenfd+1;i<=maxfd;i++){
			if(fds[i].revents&POLLIN){
				//调用recv
				ret = recv(i, buff,BUFFER_LENGTH, 0);
				if(ret>0){
					buff[ret]='\n';
					printf("recv msg from client: %s\n", buff);
					fflush(stdout);
					send(i, buff, ret, 0);
				}else if(ret==0){
					fds[i].fd=-1;
					close(i);
				}else{

				}	
			}
			if (--nready == 0) break;
		}
	}

	
    //关闭套接字
    close(listenfd);
    return 0;
}