#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/poll.h>
#include <sys/epoll.h>
#include <errno.h>
#define BUFFER_LENGTH 20
#define POLL_SIZE 65535
#define ITEM_LEGHTH 1024
//实现服务器百万并发

//需要改进的
//1 实现扩容机制
//2 扩展成监听多个服务器端口

//定义一个fd的管理块
typedef struct sock_item{ //conn_item
	int fd;//句柄
	char *rbuffer;
	int rlength;
	char *wbuffer;
	int wlength;
	int event; //事件
	//回调函数
	// void (*recv_cb)(int fd,char *buffer,int length ); 
	// void (*send_cb)(int fd,char *buffer,int length ); 
	// void (*accept_cb)(int fd,char *buffer,int length); 
}sock_item;

//这个结构体用来实现扩容
typedef struct  eventblock
{
	sock_item* items;  //ITEM_LEGHTH 个管理块
	struct eventblock* next;  //指向下一个EventBlock 串成链表
	/* data */
}eventblock ;

typedef struct reactor //可以做成单例模式
{
	// Reactor(){
	// 	epfd =epoll_create(1);
	// 	first=NULL;
	// 	blkcnt=0;
	// }
	int epfd;
	eventblock* first; //指向第一个EventBlock 串成链表
	int blkcnt;
	//
	/* data */
}reactor;

//在反应堆中通过fd找到对应的管理块

#if 1
sock_item* reactor_lookup(reactor *r,int fd){
	int col=fd/ITEM_LEGHTH;
	int row=fd%ITEM_LEGHTH;
	// printf("reactor_lookup col=%d,row=%d\n",col,row);
	fflush(stdout);
	while(col+1>r->blkcnt){
		int error=reactor_resize(r);
		if(error!=0) return NULL;
	}
	int i=0;
	eventblock* find=r->first;
	for(;i<col&&find!=NULL;i++,find=find->next);
	return &(find->items[row]);
}
//实现一个扩容的函数
int reactor_resize(reactor* r){
	if(r==NULL) return -1;
	// printf("resize\n");
	fflush(stdout);
	//申请空间
	sock_item* items=(sock_item*)malloc(ITEM_LEGHTH*sizeof(sock_item));
	if(items==NULL){
		printf("items申请失败");
		fflush(stdout);
		return -2;
	}
	memset(items,0,ITEM_LEGHTH*sizeof(sock_item));
	eventblock* eb=(eventblock*)malloc(sizeof(eventblock));
	if(eb==NULL){
		printf("eventblock申请失败");
		fflush(stdout);
		free(items);
		return -3;
	}
	memset(eb,0,sizeof(eventblock));
	eb->items=items;	

	//找到队尾
	if(r->first==NULL){
		r->first=eb;
	}else{
		eventblock* tmp=r->first;
		while(tmp->next!=NULL){
			tmp=tmp->next;
		}
		tmp->next=eb;
	}
	r->blkcnt++;
	return 0; 
}
#else if  0
int reactor_resize(struct reactor *r) { // new eventblock

	if (r == NULL) return -1;	

	struct eventblock *blk = r->first;

	while (blk != NULL && blk->next != NULL) {
		blk = blk->next;
	}

	struct sock_item* item = (struct sock_item*)malloc(ITEM_LEGHTH * sizeof(struct sock_item));
	if (item == NULL) return -4;
	memset(item, 0, ITEM_LEGHTH * sizeof(struct sock_item));

	//printf("-------------\n");
	struct eventblock *block = malloc(sizeof(struct eventblock));
	if (block == NULL) {
		free(item);
		return -5;
	}
	memset(block, 0, sizeof(struct eventblock));

	block->items = item;
	block->next = NULL;

	if (blk == NULL) {
		r->first = block;
	} else {
		blk->next = block;
	}
	r->blkcnt ++;

	return 0;
}


struct sock_item* reactor_lookup(struct reactor *r, int sockfd) {

	if (r == NULL) return NULL;
	//if (r->evblk == NULL) return NULL;
	if (sockfd <= 0) return NULL;

	//printf("reactor_lookup --> %d\n", r->blkcnt); //64
	int blkidx = sockfd / ITEM_LEGHTH;
	while (blkidx >= r->blkcnt) {
		reactor_resize(r);
	}

	int i = 0;
	struct eventblock *blk = r->first;
	while (i ++ < blkidx && blk != NULL) {
		blk = blk->next;
	}

	return  &blk->items[sockfd % ITEM_LEGHTH];
}
#endif 
// unsigned char rbuff[BUFFER_LENGTH];
// unsigned char wbuff[BUFFER_LENGTH];
// react 每一个fd 对应着不同的buffer 相当于数据隔离
// 更偏向业务的处理
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
	reactor* r =(reactor*)malloc(sizeof(reactor));
	r->epfd=epfd;
	r->blkcnt=0;
	//添加一个最初的事件
	struct epoll_event ev,events[POLL_SIZE];
	ev.events=EPOLLIN;
	ev.data.fd=listenfd;
	//将事件添加进epoll
	epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd,&ev);

	while(1){	
		//-1没事件就一直阻塞 大于0的数就是等待固定的时间结束等待 可以做定时器
		int nready=epoll_wait(epfd,events,POLL_SIZE,-1);
		int i;
		// int ret;
		for( i=0;i<nready;i++){
			int clientfd=events[i].data.fd;
			if(listenfd==clientfd){ //需要accept
				struct sockaddr_in client;
				socklen_t len = sizeof(client);
				int connfd;
				if ((connfd = accept(listenfd, (struct sockaddr *)&client, &len)) == -1) {
					// printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
					return 0;
				}
				if(connfd%1000==0){
					printf("client %d join\n",connfd);
				}
				// printf("client %d join\n", connfd);
				//将客户端加入poll池
				ev.events=EPOLLIN;
				ev.data.fd=connfd;
				epoll_ctl(epfd,EPOLL_CTL_ADD,connfd,&ev);

				//这里找到对应的sock_item并申请缓冲空间
				sock_item* sock_client=reactor_lookup(r,connfd);
				if(sock_client==NULL){
					printf("debug sock_client apply faild\n");
					fflush(stdout);
				}else{
					// printf("debug sock_client=%p\n",(void *)sock_client);
					// printf("debug items=%p\n",(void *)sock_client->r);
					fflush(stdout);
				}

				sock_client->fd=connfd;
				sock_client->rbuffer=calloc(1, BUFFER_LENGTH);
				sock_client->rlength=0;
				sock_client->wbuffer=calloc(1, BUFFER_LENGTH);
				sock_client->wlength=0;

			}else if(events[i].events&EPOLLIN){
				//这里先获取到sock_item
				sock_item* sock_client=reactor_lookup(r,clientfd);
				char *rbuff=sock_client->rbuffer;//拿到缓冲区指针
				int ret = recv(clientfd, rbuff,BUFFER_LENGTH, 0);
				sock_client->rlength=ret;  //设置长度
				if(ret>0){
					rbuff[ret]='\n'; //避免脏数据
					// printf("recv msg from client: %s\n", rbuff);
					fflush(stdout);
					//在这里修改fd的状态为写
					ev.events=EPOLLOUT;
					ev.data.fd=clientfd;
					epoll_ctl(epfd,EPOLL_CTL_MOD,clientfd,&ev);
					
					//高并发时send不能这样做可能会出现send发送数据发送不完的问题 应该将可读的监听关闭 开启可写的监听 然后再分一个if出去判断可写 这样可以多次将send的数据发送完为止
					// send(clientfd, buff, ret, 0);
				}else if(ret==0){
					// fds[i].fd=-1;
					ev.events = EPOLLIN;
					ev.data.fd = clientfd;
					epoll_ctl(epfd, EPOLL_CTL_DEL, clientfd, &ev);
					// epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd,&ev);
					close(clientfd);
				}
			}else if(events[i].events&EPOLLOUT){  
				sock_item* sock_client=reactor_lookup(r,clientfd);
				char *wbuff=sock_client->wbuffer;//拿到缓冲区指针
				// printf("client %d write %d\n",sock_client->fd,sock_client->rlength);
				fflush(stdout);
				send(clientfd, wbuff, sock_client->rlength, 0);
				ev.events=EPOLLIN;
				ev.data.fd=clientfd;
				epoll_ctl(epfd,EPOLL_CTL_MOD,clientfd,&ev);
			}
		}

	}
    //关闭套接字
    close(listenfd);
    return 0;
}