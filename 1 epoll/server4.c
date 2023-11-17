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

    //用poll来实现
    // int POLL_SIZE=10;
    struct pollfd fds[POLL_SIZE] = {0};
	fds[0].fd = listenfd;
	fds[0].events = POLLIN;

	int max_fd = listenfd;
	int i = 0;
    int n;
    unsigned char buff[BUFFER_LENGTH];
	for (i = 1;i < POLL_SIZE;i ++) {
		fds[i].fd = -1;
	}

	while (1) {

		int nready = poll(fds, max_fd+1, -1);

	
		if (fds[0].revents & POLLIN) {

			struct sockaddr_in client;
		    socklen_t len = sizeof(client);
            int connfd;
		    if ((connfd = accept(listenfd, (struct sockaddr *)&client, &len)) == -1) {
		        printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
		        return 0;
		    }

			printf("accept \n");
			fds[connfd].fd = connfd;
			fds[connfd].events = POLLIN;

			if (connfd > max_fd) max_fd = connfd;

			if (--nready == 0) continue;
		}

		//int i = 0;
		for (i = listenfd+1;i <= max_fd;i ++)  {

			if (fds[i].revents & POLLIN) {
				
				n = recv(i, buff, BUFFER_LENGTH, 0);
		        if (n > 0) {
		            buff[n] = '\0';
		            printf("recv msg from client: %s\n", buff);

					send(i, buff, n, 0);
		        } else if (n == 0) { //

					fds[i].fd = -1;

		            close(i);
					
		        }
				if (--nready == 0) break;

			}

		}

	}
    
    //关闭套接字
    close(listenfd);
    return 0;
}