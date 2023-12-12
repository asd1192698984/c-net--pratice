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


#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#define BUFFER_LENGTH 1024
#define MAX_EPOLL_EVENTS 1024
#define RESOURECE_LENGTH 1024

#define SERVER_PORT 8888
#define PORT_COUNT 1
#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef int NCALLBACK(int,int,void*);
int recv_cb(int fd, int events, void *arg);
int send_cb(int fd, int events, void *arg);

//定义当前报文的状态常量
enum WS_STAT{
    WS_HANDSHARK=0,
    WS_TRANSMISSION=1,
    WS_END=2,
};



//报文的前两个字节
typedef struct _ws_ophdr {
	
	unsigned char opcode:4,
				  rsv3:1,
				  rsv2:1,
				  rsv1:1,
				  fin:1;
	unsigned char pl_len:7,
				  mask:1;
} ws_ophdr;

//负载为126情况
typedef struct _ws_head_126 {

	unsigned short payload_length;
	char mask_key[4];

} ws_head_126;
//负载为127情况
typedef struct _ws_head_127 {

	long long payload_length;
	char mask_key[4];

} ws_head_127;


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

    //websocket
    enum WS_STAT ws_stat;
    int is_close;
};



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
/**
 * 读取http报文中的一行
 * @param linebuf 一行存放位置 
 * @param idx 从buffer哪里开始读
 * @param buffer 读缓冲区
 * @return 下一行开始的位置 读完返回-1
*/
int readline(char *buffer,int idx,char*linebuf ){
    int i=0;
    int len =strlen(buffer); //获取buffer字符串总长度
    for(;idx<len;){
        if(buffer[idx]=='\r'&&buffer[idx+1]=='\n'){
            linebuf[i]='\0';
            return idx+2;
        }else{
            linebuf[i++]=buffer[idx++];
        }
    }
    return -1;
}


//base64加密
int base64_encode(char *in_str, int in_len, char *out_str) {    
	BIO *b64, *bio;    
	BUF_MEM *bptr = NULL;    
	size_t size = 0;    

	if (in_str == NULL || out_str == NULL)        
		return -1;    

	b64 = BIO_new(BIO_f_base64());    
	bio = BIO_new(BIO_s_mem());    
	bio = BIO_push(b64, bio);
	
	BIO_write(bio, in_str, in_len);    
	BIO_flush(bio);    

	BIO_get_mem_ptr(bio, &bptr);    
	memcpy(out_str, bptr->data, bptr->length);    
	out_str[bptr->length-1] = '\0';    
	size = bptr->length;    

	BIO_free_all(bio);    
	return size;
}
//解掩码操作
void ws_umask(char *payload, int length, char *mask_key){
    int i;
    for( i=0;i<length;i++){
        int j=i%4;
        payload[i]^=mask_key[j];
    }
}
void handshark(struct ntyevent *ev){
    //握手设置为打开
    ev->is_close=0;
    //1 解析报文拿到key
     printf("handshark\n");
    char line[512]={0};
    char key[512]={0};
    char key2[128]={0};
    char sec_accept[128]={0};
    int idx=0;
    while((idx=readline(ev->rbuffer,idx,line))!=-1){
        //  printf("%s\n",line);
        if(strstr(line,"Sec-WebSocket-Key")){   
             printf("%s\n",line);
            int len=strlen(line);
            int s=strlen("Sec-WebSocket-Key")+2; //key的起始位置
            int key_len=strlen(line)-s;  //算出key的长度
            memcpy(key,line+s,key_len);
            break;
            // printf("%s\n",key);
            // sprintf(key);
        }
    }
    //2 对key进行加密
    //首先跟GUID进行连接
    strcat(key,GUID);
    //SHA1进行哈希
    SHA1(key,strlen(key),key2);
    // printf("key2 %s\n",key2);
    //base64进行加密
    base64_encode(key2,strlen(key2),sec_accept);
    //3 返回的报文中加上加密后的key
    ev->wlength = sprintf(ev->wbuffer, "HTTP/1.1 101 Switching Protocols\r\n"
					"Upgrade: websocket\r\n"
					"Connection: Upgrade\r\n"
					"Sec-WebSocket-Accept: %s\r\n\r\n", sec_accept);
    // printf("ws response : %s\n", ev->wbuffer);
    printf("handshark success\n");
}
void transmission(struct ntyevent *ev){
    //1 拿到传输过来的信息
    ws_ophdr* head=(ws_ophdr*)ev->rbuffer;  //转换前两个字节
    printf("fin=%d rsv1=%d rsv2=%d rsv3=%d  opcode=%d mask=%d pl_len=%d\n",head->fin,head->rsv1,head->rsv2,head->rsv3,head->opcode,head->mask,head->pl_len);
    if(head->opcode==8){ //关闭连接
       ev->is_close=1;
       ev->ws_stat=WS_HANDSHARK;
       return ;
    }
    //根据pl_len选择合适的结构体
    char *payload=NULL;
    char * mask=NULL;
    int pl_len=head->pl_len;
    if(pl_len>0&&pl_len<126){
         payload =ev->rbuffer+sizeof(ws_ophdr)+4; 
         mask=ev->rbuffer+2;
    }else if(pl_len==126){  //126-2的16次方数据

    }else if(pl_len==127){ //2的16次方到2的64次方

    }
    //2 进行解密
    if(head->mask){
       ws_umask(payload,pl_len,mask);
    }
    printf("payload = %s\n",payload);
    //3 加密信息，传送回去
    strcat(payload,"fromserver");
    printf("after strcat payload = %s\n",payload);
    //虽然是测试但是还是判断一下长度
    pl_len=strlen(payload);
    // printf("after strcat payload = %s\n",payload);
    //协议解析错误 去掉掩码试试
    //head->mask=1;
    if(pl_len>0&&pl_len<126){
        //加密payload
        // if(head->mask)
        // ws_umask(payload,pl_len,mask);  
        //写入头
        head->fin=1;
        head->rsv1=head->rsv2=head->rsv3=0;
        head->opcode=1; //文本传输
        head->mask=0;
        head->pl_len=pl_len;
        memcpy(ev->wbuffer,head,sizeof(ws_ophdr));
        //写入mask
        // memcpy(ev->wbuffer+sizeof(ws_ophdr),mask,4);
        //写入payload
        memcpy(ev->wbuffer+sizeof(ws_ophdr),payload,pl_len);
        ev->wlength=sizeof(ws_ophdr)+4+pl_len;
    }else if(pl_len==126){  //126-2的16次方数据

    }else if(pl_len==127){ //2的16次方到2的64次方

    }
    
}
void wshandle(struct ntyevent *ev){
    if(ev->ws_stat==WS_HANDSHARK){
        handshark(ev);
        ev->ws_stat=WS_TRANSMISSION;
    }else if(ev->ws_stat==WS_TRANSMISSION){
        transmission(ev);
    }
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
         printf("resv from %d\n %d byte\n",fd,len);
         fflush(stdout);
        //处理ws请求
        wshandle(ev);
        if(ev->is_close){ //客户端关闭连接
            nty_event_del(reactor->epfd, ev); //移除监听
		    close(ev->fd); //释放连接
            printf("close %d\n",fd);
            fflush(stdout);
        }else{
             nty_event_set(ev, fd, send_cb, reactor); //设置发送数据的回调
		    nty_event_add(reactor->epfd, EPOLLOUT, ev); //监听发送
        }
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
    // printf("buffer %s  length %d\n",ev->rbuffer,ev->rlength);
    int len=0;
    if(ev->wlength>0){
        len = send(fd, ev->wbuffer, ev->wlength, 0);
         printf("send to %d %d byte\n",fd,len);
    }
    fflush(stdout);
    if(len>0){
        nty_event_del(reactor->epfd, ev);
		nty_event_set(ev, fd, recv_cb, reactor);
		nty_event_add(reactor->epfd, EPOLLIN, ev);
    }else if(len<0) {//出错 关闭连接
        nty_event_del(reactor->epfd, ev);
		close(ev->fd);
    }
    //避免重复发送
    ev->wlength=0;
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