#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/stat.h>


#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include <sys/sendfile.h>

#define BUFFER_LENGTH 1024
#define MAX_EPOLL_EVENTS 1024
#define RESOURECE_LENGTH 1024

#define SERVER_PORT 8888
#define PORT_COUNT 1

#define HTTP_METHOD_GET 0
#define HTTP_METHOD_POST 1

#define HTTP_RESOURCE_ROOT "/home/mxl/workspace/c++/2 net/3 http/resource"
typedef int NCALLBACK(int,int,void*);
int recv_cb(int fd, int events, void *arg);
int send_cb(int fd, int events, void *arg);

struct ntyevent
{
    /* data */
    int fd;
    int events;  //事件
    void *arg; //回调函数参数
    int (*callback)(int fd,int events,void *arg);

    int status; //ntyevent是否是第一次添加在epoll池中
    char rbuffer[BUFFER_LENGTH];
	
	char wbuffer[BUFFER_LENGTH];

    int rlength;
    int wlength;

    //http
    int method; //请求的方法
    char resource[RESOURECE_LENGTH]; //储存请求的资源
    int issend; //发送文件类型
};



/**
 * 接收一个缓冲区的起始地址和开始读取位置，用于读取以\r\n结尾的一行
 * 将读取的数据放入line中，所以需要确保line中已经分配好了空间
 * @return 下一行的起始字符 如果读完了或者解析错误返回-1
*/
int readline(char *line,const char * msg,int idx){
    int i=0;
    int len=strlen(msg); //获取字符串长度 避免越界
    while(idx+1<len&&msg[idx]!='\r'&&msg[idx+1]!='\n'){
        line[i++]=msg[idx++];
    }
    line[i]='\0';//字符串结尾
    idx=idx+2; //下一行的开始
    if(idx+1<len&&msg[idx]!='\r'&&msg[idx]!='\n'){
        return idx;
    }else{
        return -1;
    }
}
/**
 * 读取的数据在ev的rbuffer中
 * 该函数解析rbuffer的http请求
*/
void parser_http_request(struct ntyevent* ev){
    char linebuffer[1024]={0};
    //测试
    // int idx=0;
    // while((idx=readline(linebuffer,ev->rbuffer,idx))!=-1){
    //     printf("%s\n",linebuffer);
    //     //处理每一行 
    // }
    //处理第一行的协议头
    readline(linebuffer,ev->rbuffer,0);
    if(strstr(linebuffer,"GET")){
        ev->method=HTTP_METHOD_GET;
        //解析请求的资源 
        int l=4,s=4; //跳过四个字符GET空格 s是请求路径的开始 ，l是结束的下一个位置
        while(linebuffer[l]!=' ')l++;
        linebuffer[l]='\0';
        sprintf(ev->resource,"%s%s",HTTP_RESOURCE_ROOT,linebuffer+s);
        printf("resource %s\n",ev->resource);
    }else if(strstr(linebuffer,"POST")){
        ev->method=HTTP_METHOD_POST;
    }
}
void http_get_response(struct ntyevent* ev){
    int filefd=open(ev->resource,O_RDONLY);  //open打开只读文件
    int len;
    if(filefd==-1){ //找不到对应的html资源
        len=sprintf(ev->wbuffer,"HTTP/1.1 200 OK\r\n"
"Accept-Ranges: bytes\r\n"
"Content-Length: 78\r\n"
"Content-Type: text/html\r\n"
"Date: Sat, 06 Aug 2022 13:16:46 GMT\r\n\r\n"
"<html><head><title>404 error</title></head><body><h1>404 error</h1><body/></html>");
        ev->wlength=len;
        ev->issend=0;
    }else{
        //读取文件信息
        struct stat stat_buf;
        fstat(filefd,&stat_buf); //读取文件信息
        close(filefd);
        len=sprintf(ev->wbuffer,"HTTP/1.1 200 OK\r\n"
"Accept-Ranges: bytes\r\n"
"Content-Length: %ld\r\n"
"Content-Type: text/html\r\n"
"Date: Sat, 06 Aug 2022 13:16:46 GMT\r\n\r\n"
,stat_buf.st_size);
        ev->wlength=len;
        //这里标识发送的资源类型
        ev->issend=1;
    }
}
void http_post_response(struct ntyevent* ev){

}
void http_response(struct ntyevent* ev){
    if(ev->method==HTTP_METHOD_GET){
        http_get_response(ev);
    }else if(ev->method==HTTP_METHOD_POST){
        http_post_response(ev);
    }
}

struct  eventblock
{
     /* data */
    struct eventblock *next;
	struct ntyevent *events;
};


struct ntyreactor {
	int epfd;
	int blkcnt;

	struct eventblock *evblks;
};


struct timeval tv_bgin;

void nty_event_set(struct ntyevent *ev,int fd,NCALLBACK callback,void *arg){
    if(ev!=NULL){
        ev->fd = fd;
        ev->callback = callback;
        ev->events = 0;
        ev->arg = arg;
    }
    return ;
}


// typedef union epoll_data {
// void *ptr;
// int fd;
// __uint32_t u32;
// __uint64_t u64;
// } epoll_data_t;//保存触发事件的某个文件描述符相关的数据
// struct epoll_event {
// __uint32_t events; /* epoll event */
// epoll_data_t data; /* User data variable */
// };
//将事件添加到epoll池中
//这里传入的events可能会修改ev本身的events
int nty_event_add(int epfd,int events,struct ntyevent *ev){
    struct epoll_event ep_ev ={0,{0}}; //结构体初始化
    ep_ev.data.ptr=ev;  //在epoll_event中储存控制块的指针
    ep_ev.events=ev->events=events;  //修改epoll和我们定义的两个控制块的事件

    int op;
    if(ev->status==1){ //刚开始是0 加入epoll池中变为1 
        op=EPOLL_CTL_MOD;
    }else{ //是0就加入
        op=EPOLL_CTL_ADD;
        ev->status=1;    
    }
    //调用epoll_ctl
    if(epoll_ctl(epfd,op,ev->fd,&ep_ev)<0){
        printf("event add failed [fd=%d], events[%d]\n", ev->fd, events);
        return -1;
    }
    return 0;
}

int nty_event_del(int epfd, struct ntyevent *ev){
    struct epoll_event ep_ev ={0,{0}}; //结构体初始化
    if(ev->status!=1){ //对应fd不在epoll池中
        return -1;
    }
    ep_ev.data.ptr=ev;  //在epoll_event中储存控制块的指针
    ev->status=0;
    epoll_ctl(epfd,EPOLL_CTL_DEL,ev->fd,&ep_ev);
    return 0;
}




//三个回调函数的实现 read_cb send_cb accept_cb




//监听服务端的一个本地端口
int init_sock(short port){
    //先创建socket 然后bind绑定本地端口 ，然后调用listen
    int fd=socket(AF_INET,SOCK_STREAM,0);
    //服务端的accept阻不阻塞感觉没什么影响,因为主要是epoll_wait阻塞了,
    //epoll_wait结束阻塞时直接调用accept接受连接就行
    fcntl(fd, F_SETFL, O_NONBLOCK); //设置非阻塞型IO
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    //bind绑定
    bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    //listen
    if(listen(fd,20)<0){
        printf("listen failed : %s\n", strerror(errno));
		return -1;
    }
    printf("listen server port : %d\n", port);

    //记录当前时间
    gettimeofday(&tv_bgin,NULL);
    return fd;
}

//动态扩容
int ntyreactor_alloc(struct ntyreactor *reactor) {

	if (reactor == NULL) return -1;
	if (reactor->evblks == NULL) return -1;
	
	struct eventblock *blk = reactor->evblks;

	while (blk->next != NULL) {
		blk = blk->next;
	}

	struct ntyevent* evs = (struct ntyevent*)malloc((MAX_EPOLL_EVENTS) * sizeof(struct ntyevent));
	if (evs == NULL) {
		printf("ntyreactor_alloc ntyevent failed\n");
		return -2;
	}
	memset(evs, 0, (MAX_EPOLL_EVENTS) * sizeof(struct ntyevent));

	struct eventblock *block = malloc(sizeof(struct eventblock));
	if (block == NULL) {
		printf("ntyreactor_alloc eventblock failed\n");
		return -3;
	}
	block->events = evs;
	block->next = NULL;

	blk->next = block;
	reactor->blkcnt ++;

	return 0;
}

//通过fd找到对应的ntyevent
struct ntyevent *ntyreactor_idx(struct ntyreactor *reactor, int sockfd) {

	if (reactor == NULL) return NULL;
	if (reactor->evblks == NULL) return NULL;

	int blkidx = sockfd / MAX_EPOLL_EVENTS;
	while (blkidx >= reactor->blkcnt) {
		ntyreactor_alloc(reactor);
	}

	int i = 0;
	struct eventblock *blk = reactor->evblks;
	while (i++ != blkidx && blk != NULL) {
		blk = blk->next;
	}

	return &blk->events[sockfd % MAX_EPOLL_EVENTS];
}


//初始化reactor
int ntyreactor_init(struct ntyreactor *reactor) {
    //需要初始化的东西 epfd
    if (reactor == NULL) return -1;
	memset(reactor, 0, sizeof(struct ntyreactor));
    //epfd;
    reactor->epfd = epoll_create(1);
    if (reactor->epfd <= 0) {
		printf("create epfd in %s err %s\n", __func__, strerror(errno));
		return -2;
	}
    //申请空间 感觉不需要申请 
    //需要申请 不然影响后面的函数
    struct ntyevent* evs = (struct ntyevent*)malloc((MAX_EPOLL_EVENTS) * sizeof(struct ntyevent));
	if (evs == NULL) {
		printf("create epfd in %s err %s\n", __func__, strerror(errno));
		close(reactor->epfd);
		return -3;
	}
	memset(evs, 0, (MAX_EPOLL_EVENTS) * sizeof(struct ntyevent));

	struct eventblock *block = malloc(sizeof(struct eventblock));
	if (block == NULL) {
		free(evs);
		close(reactor->epfd);
		return -3;
	}
	block->events = evs;
	block->next = NULL;

	reactor->evblks = block;
	reactor->blkcnt = 1;
    return 0;
}


//主要是释放资源
int ntyreactor_destory(struct ntyreactor *reactor) {
    close(reactor->epfd);

    struct eventblock * blk=reactor->evblks;
    struct eventblock * next;
    while(blk!=NULL){
        next=blk->next;
        free(blk->events);
        free(blk);
        blk=next;
    }
    return 0;
}

//传入fd 监听事件 监听回调 设置监听
int ntyreactor_addlistener(
    struct ntyreactor *reactor, int sockfd,int events,NCALLBACK *acceptor) {
    if (reactor == NULL) return -1;
	if (reactor->evblks == NULL) return -1;
    //查找到对应的event控制块
    struct ntyevent *event = ntyreactor_idx(reactor, sockfd);
    //设置回调 set方法并没有传入监听事件
    nty_event_set(event,sockfd,acceptor,reactor); 
    //传入监听事件
    nty_event_add(reactor->epfd,events,event);
	return 0;
}

//启动reactor
int ntyreactor_run(struct ntyreactor *reactor) {
    if (reactor == NULL) return -1;
	if (reactor->epfd < 0) return -1;
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int i;
    while(1){
        int nready =epoll_wait(reactor->epfd,events,MAX_EPOLL_EVENTS,1000);
        if(nready<0){
            printf("epoll_wait error, exit\n");
			continue;
        }
        //循环处理事件
        for(i=0;i<nready;i++){
            //直接从epoll_event中拿到对应的控制块
            struct ntyevent *ev=(struct ntyevent*)events[i].data.ptr;
            // 什么时候处理
            if((events[i].events&EPOLLIN)&&(ev->events&EPOLLIN)||
            (events[i].events&EPOLLOUT)&&(ev->events&EPOLLOUT)
            ){
                ev->callback(ev->fd,events[i].events,ev->arg);
            }
        }
    }

	return 0;
}

//接收数据的函数
int recv_cb(int fd, int events, void *arg) {
    //通过fd找到对应的控制块
    struct ntyreactor *reactor = (struct ntyreactor*)arg;
	struct ntyevent *ev = ntyreactor_idx(reactor, fd);
    if (ev == NULL) return -1;
    //调用resv函数
    int len = recv(fd, ev->rbuffer, BUFFER_LENGTH, 0);
    nty_event_del(reactor->epfd, ev);//为什么先要移除监听?不是直接可以修改
    if(len>0){
        ev->rlength = len;
		ev->rbuffer[len] = '\0';  //设置结束符号

        parser_http_request(ev);  //解析http请求
        
        // printf("resv from %d\n %s",fd,ev->rbuffer);
         fflush(stdout);
		nty_event_set(ev, fd, send_cb, reactor); //设置发送数据的回调
		nty_event_add(reactor->epfd, EPOLLOUT, ev); //监听发送
    }else if(len==0){
        nty_event_del(reactor->epfd, ev); //移除监听
		close(ev->fd); //释放连接
    }else{ //出错
        if (errno == EAGAIN && errno == EWOULDBLOCK) { //
			//表示资源暂时不可用。在非阻塞套接字上调用 recv 函数时，
            //如果没有数据可读，recv 函数会返回 EAGAIN 或 EWOULDBLOCK 错误码，
            //表示暂时没有数据可用。这并不表示发生了错误，而是需要稍后再次尝试读取数据。
		} else if (errno == ECONNRESET){
            //：这个错误码表示连接被对方重置（reset）。
            //在网络编程中，当远程主机（对方）意外关闭连接或发生连接中断时，
            //本地套接字可能会接收到 ECONNRESET 错误码。这通常表示连接的一方意外
            //终止了连接，可能是由于网络故障、超时、或对方进程崩溃等原因
			nty_event_del(reactor->epfd, ev); //移除监听
			close(ev->fd); //释放连接
		}
    }
    return len;
}
//发送数据的函数
int send_cb(int fd, int events, void *arg) {
    //先拿到控制块
    struct ntyreactor *reactor = (struct ntyreactor*)arg;  
	struct ntyevent *ev = ntyreactor_idx(reactor, fd);

	if (ev == NULL) return -1;
    //printf("buffer %s  length %d\n",ev->rbuffer,ev->rlength);
    http_response(ev);
    int len = send(fd, ev->wbuffer, ev->wlength, 0);
    printf("send to %d %d byte",fd,len);
    fflush(stdout);
    if(len>0){
        if(ev->issend){ //需要发送文件
            //先打开文件 获取文件状态
            int filefd=open(ev->resource,O_RDONLY);
            struct stat stat_buf;
            fstat(filefd,&stat_buf);
            
            //修改文件为阻塞
            int flag=fcntl(filefd,F_GETFL,0);
            flag &=~O_NONBLOCK;
            fcntl(filefd,F_SETFL,flag);
            
            //调用sendfile
            int ret=sendfile(fd,filefd,NULL,stat_buf.st_size);
            if (ret == -1) {
			printf("sendfile: errno: %d\n", errno);
		    }
            
            //修改文件状态为非阻塞
            flag |=O_NONBLOCK;
            fcntl(filefd,F_SETFL,flag);
           
            //关闭文件
            close(filefd);
            //发送换行
            send(fd, "\r\n", 2, 0);
            //发送状态复位
            ev->issend=0; 
        }
        nty_event_del(reactor->epfd, ev);
		nty_event_set(ev, fd, recv_cb, reactor);
		nty_event_add(reactor->epfd, EPOLLIN, ev);
    }else{//出错 关闭连接
        nty_event_del(reactor->epfd, ev);
		close(ev->fd);
    }
    return len;
}


int curfds = 0;

#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
//接收客户端连接的回调
int accept_cb(int fd, int events, void *arg) {
    //先判断
    struct ntyreactor *reactor =(struct ntyreactor *)arg;
    if (reactor == NULL) return -1;
    //然后接受连接 拿到用户端fd
    struct sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);
    int clientfd;
	if ((clientfd = accept(fd, (struct sockaddr*)&client_addr, &len)) == -1) {
		if (errno != EAGAIN && errno != EINTR) {
			
		}
		printf("accept: %s\n", strerror(errno));
		return -1;
	}
    printf("client %d join\n",clientfd);
    fflush(stdout);
    //接着改为非阻塞IO
    int flag =0;
    if((flag=fcntl(clientfd,F_SETFL,O_NONBLOCK))<0){
        printf("%s: fcntl nonblocking failed, %d\n", __func__, MAX_EPOLL_EVENTS);
		return -1;
    }

    //最后将客户端的fd加入poll池 且初始化fd的控制块
    //先查找到控制块
    struct ntyevent *event = ntyreactor_idx(reactor, clientfd);
    if (event == NULL) return -1;

    nty_event_set(event,clientfd,recv_cb,reactor);
    nty_event_add(reactor->epfd,EPOLLIN,event);

    //下面是打印时间的操作
    if (curfds++ % 1000 == 999) {//每1000个打印一次 记录一下连接1000个客户端所需要的时间
		struct timeval tv_cur;
		memcpy(&tv_cur, &tv_bgin, sizeof(struct timeval));
		
		gettimeofday(&tv_bgin, NULL);

		int time_used = TIME_SUB_MS(tv_bgin, tv_cur);
		printf("connections: %d, sockfd:%d, time_used:%d\n", curfds, clientfd, time_used);
	}
   
}


int main(int argc,char * argv[]){
    //先创建reactor
    struct ntyreactor *reactor=(struct ntyreactor *) malloc(sizeof (struct ntyreactor));
    ntyreactor_init(reactor);

    unsigned short port =SERVER_PORT;
    if(argc==2){ //手动指定了端口
        port=atoi(argv[1]);
    }
    //创建监听的socket
    int i=0;
    int sockfds[PORT_COUNT]={0};

    for(int i=0;i<PORT_COUNT;i++){
        sockfds[i]=init_sock(port+i);
        //绑定监听事件 将fd加入epoll池
        ntyreactor_addlistener(reactor,sockfds[i],EPOLLIN,accept_cb);
    }

    ntyreactor_run(reactor);

    ntyreactor_destory(reactor);

    //释放资源
    for(int i=0;i<PORT_COUNT;i++){
       close(sockfds[i]);
    }
    free(reactor);
}