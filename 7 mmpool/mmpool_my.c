#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>


#define MP_PAGE_SIZE			4096
#define MP_ALIGNMENT            32
#define MP_MAX_ALLOC_FROM_POOL	(MP_PAGE_SIZE-1)

#define mp_align(n, alignment) (((n)+(alignment-1)) & ~(alignment-1))
#define mp_align_ptr(p, alignment) (void *)((((size_t)p)+(alignment-1)) & ~(alignment-1))

typedef struct mp_large_s {  //大块内存
	struct mp_large_s *next;
	void *alloc;
}mp_large_s;

typedef struct mp_node_s {  //小块内存

	unsigned char *last;  //指向可用内存的开头
	unsigned char *end;	  //指向可用内存的结尾
	
	struct mp_node_s *next;
	size_t failed;
}mp_node_s;

typedef struct mp_pool_s { 

	 size_t max;

	struct mp_node_s *current;  //指向当前使用的小内存块
	struct mp_large_s *large;   //指向大块内存链表的开头

	 struct mp_node_s head[0];

}mp_pool_s;

struct mp_pool_s *mp_create_pool(size_t size);
void mp_destory_pool(struct mp_pool_s *pool);
void *mp_alloc(struct mp_pool_s *pool, size_t size);
void *mp_nalloc(struct mp_pool_s *pool, size_t size);
void *mp_calloc(struct mp_pool_s *pool, size_t size);
void mp_free(struct mp_pool_s *pool, void *p);

static void *mp_alloc_block(struct mp_pool_s *pool, size_t size);
static void *mp_alloc_large(struct mp_pool_s *pool, size_t size);
//创建一个内存池 size是内存池的小块用户空间的大小
struct mp_pool_s *mp_create_pool(size_t size){
	struct mp_pool_s *p; //内存池的指针
	//给内存池结构体分配空间
	//传入的是二级指针，因为我们要修改指针的内容  
	//MP_ALIGNMENT是对齐的地址大小，代表以32的倍数对齐
	int ret =posix_memalign((void**)&p,MP_ALIGNMENT,size + sizeof(struct mp_pool_s) + sizeof(struct mp_node_s));
	if(ret){
		return NULL;
	}
	//MP_MAX_ALLOC_FROM_POOL=4095 4096就分配大块 
	//size 小于4095 max 就等于size 否则就等于4095
	p->max=(size<MP_MAX_ALLOC_FROM_POOL)?size:MP_MAX_ALLOC_FROM_POOL;
	//这一部分结合图理解 p的current指针指向当前正在用的小块内存区域
	p->current=p->head;
	//大块内存区域还没有数据
	p->large=NULL;
	//转化为char 类型指针是为了让指针的加法以1为偏移量
	//last指向了用户空间的 开头位置
	p->head->last=(unsigned char *)p+sizeof(struct mp_pool_s)+sizeof(mp_node_s);
	//end指向了用户空间的末尾
	p->head->end=p->head->last+size;
	//这个还不知道用来干嘛的
	p->head->failed=0; 
	//next初始化
	p->head->next=NULL; 
	return p;
}

//回收资源
void mp_destory_pool(struct mp_pool_s *pool){
	struct mp_node_s *h, *n;
	struct mp_large_s *l;
	
	for (l = pool->large; l; l = l->next) {
		if (l->alloc) {
			free(l->alloc);
		}
	}

	h = pool->head->next;

	while (h) {
		n = h->next;
		free(h);
		h = n;
	}

	free(pool);
}
void *mp_alloc(struct mp_pool_s *pool, size_t size){
	unsigned char *m;
	struct mp_node_s *p;
	if(size <=pool->max){
		p=pool->current; //指向小块内存的mp_pool_s结构体
		do{
			//将p->last（可分配内存的起点）移动到MP_ALIGNMENT对齐的地址 向上取整
			m=mp_align_ptr(p->last,MP_ALIGNMENT);
			//判断从p->last 开始分配有没有这么大的内存
			if((size_t)(p->end-p->last)>=size){//如果有就直接返回地址
				//更新p->last 
				p->last=m+size;
				return m;
			}else{
				p=p->next;
			}
		}while(p);
		//如果遍历完所有节点仍然没有找到足够的空间，那么会调用 mp_alloc_block 函数来分配一个新的内存块，并返回分配的内存空间。
		//有个问题 p->current在什么时候向后移动  在这里p->current始终指向头节点
		return mp_alloc_block(pool,size);
	}else{ //分配大块内存
		return mp_alloc_large(pool,size);
	}
}

//和mp_alloc 类似 但是分配的size的起始位置不进行内存对齐
void *mp_nalloc(struct mp_pool_s *pool, size_t size){
	unsigned char *m;
	struct mp_node_s *p;

	if (size <= pool->max) {
		p = pool->current;

		do {
			m = p->last;
			if ((size_t)(p->end - m) >= size) {
				p->last = m+size;
				return m;
			}
			p = p->next;
		} while (p);

		return mp_alloc_block(pool, size);
	}

	return mp_alloc_large(pool, size);
}
//这个接口就是把分配的区域初始化成0
void *mp_calloc(struct mp_pool_s *pool, size_t size){
	void *p = mp_alloc(pool, size);
	if (p) {
		memset(p, 0, size);
	}

	return p;
}


static void *mp_alloc_block(struct mp_pool_s *pool, size_t size) {
	//定义一个指针m 它指向被分配内存的开头
	unsigned char *m;
	//拿到头节点的控制块 也是小块指针链表的入口位置
	struct mp_node_s*h =pool->head;
	//计算要分配内存 应该等于 4k +sizeof(mp_node_s)
	size_t psize= (size_t)(h->end-(unsigned char *)h);

	//这里分配内存
	int ret =posix_memalign((void**)&m,MP_ALIGNMENT,psize);	
	if (ret) return NULL;

	//创建新的节点
	struct mp_node_s*p,*new_node,*current;
	//这里先将指针 m 转换为 struct mp_node_s* 类型
	//这样保证用户空间为4k
	new_node=(struct mp_node_s*)m;
	//指向用户空间的结尾
	new_node->end=m+psize;
	//next指针置为null
	new_node->next=NULL;
	new_node->failed=0;

	//这里给用户分配内存
	//将m移动到用户空间的开头位置
	m+=sizeof(struct mp_node_s);
	//对m进行指针的对齐
	m = mp_align_ptr(m, MP_ALIGNMENT);
	//更新新节点的last
	new_node->last=m+size;
	//拿到当前的current
	current = pool->current;

	//这里将刚刚创建的内存块插入链表中 尾插法
	for (p = current; p->next; p = p->next) {
		//在遍历过程中，如果节点 p 的失败计数器 failed 大于 4，则将当前节点 current 更新为 p 的下一个节点 p->next。
		//这个计数器会使得current的值前移
		//因为current之前的节点也可能存在没有用完的内存
		if (p->failed++ > 4) { 
			current = p->next;
		}
	}
	p->next = new_node;

	//current非空就把内存池的current更新
	pool->current = current ? current : new_node;

	return m;
}
//分配大块内存
static void *mp_alloc_large(struct mp_pool_s *pool, size_t size) {
	//分配 size 字节大小的内存块，并将返回的指针存储在变量 p 中
	void *p = malloc(size);
	if (p == NULL) return NULL;
	//函数遍历内存池 pool 中的大块内存链表 pool->large
	//直到找到一个未分配的大块内存或者遍历了超过 3 个大块内存。查找太多影响效率
	//如果找到了未分配的大块内存，将其 alloc 字段设置为 p，即将分配的内存块指针存储在其中，并返回该指针。
	size_t n = 0;
	struct mp_large_s *large;
	for (large = pool->large; large; large = large->next) {
		if (large->alloc == NULL) {
			large->alloc = p;
			return p;
		}
		if (n ++ > 3) break;
	}

	//在这段代码中，调用 mp_alloc 函数分配一个 sizeof(struct mp_large_s) 大小的内存块，用于存储新的大块内存结构。
	large = mp_alloc(pool, sizeof(struct mp_large_s));
	//如果内存分配失败，会释放之前分配的内存块 p，然后返回 NULL，表示分配失败。
	if (large == NULL) {
		free(p);
		return NULL;
	}
	//头插法
	large->alloc = p;
	large->next = pool->large;
	pool->large = large;

	return p;
}

//提供一个按照内存方式分配的大块内存
//因为前面写的大块内存的分配没有内存对齐 直接用的malloc
//和分配大块内存的代码类似 除了分配内存的函数不同
void *mp_memalign(struct mp_pool_s *pool, size_t size, size_t alignment) {

	void *p;
	
	int ret = posix_memalign(&p, alignment, size);
	if (ret) {
		return NULL;
	}

	struct mp_large_s *large = mp_alloc(pool, sizeof(struct mp_large_s));
	if (large == NULL) {
		free(p);
		return NULL;
	}

	large->alloc = p;
	large->next = pool->large;
	pool->large = large;
	
	return p;
}
//回收掉p对应的大块资源 测试时使用
void mp_free(struct mp_pool_s *pool, void *p) {

	struct mp_large_s *l;
	for (l = pool->large; l; l = l->next) {
		if (p == l->alloc) {
			free(l->alloc);
			l->alloc = NULL;

			return ;
		}
	}
	
}

//在这里进行reset 释放大块的资源 小块的指针全部置0但是不释放资源
void mp_reset_pool(struct mp_pool_s *pool) {

	struct mp_node_s *h;
	struct mp_large_s *l;

	for (l = pool->large; l; l = l->next) {
		if (l->alloc) {
			free(l->alloc);
		}
	}

	pool->large = NULL;

	for (h = pool->head; h; h = h->next) {
		h->last = (unsigned char *)h + sizeof(struct mp_node_s);
	}

}
//测试代码
int main(){
	int size = 1 << 12;

	struct mp_pool_s *p = mp_create_pool(size);

	int i = 0;
	for (i = 0;i < 10;i ++) {
		//分配10个小块内存
		void *mp = mp_alloc(p, 512);
//		mp_free(mp);
	}
	//测试对齐
	//printf("mp_create_pool: %ld\n", p->max);
	printf("mp_align(123, 32): %d, mp_align(17, 32): %d\n", mp_align(24, 32), mp_align(17, 32));
	//printf("mp_align_ptr(p->current, 32): %lx, p->current: %lx, mp_align(p->large, 32): %lx, p->large: %lx\n", mp_align_ptr(p->current, 32), p->current, mp_align_ptr(p->large, 32), p->large);

	//测试calloc
	int j = 0;
	for (i = 0;i < 5;i ++) {

		char *pp = mp_calloc(p, 32);
		for (j = 0;j < 32;j ++) {
			if (pp[j]) {
				printf("calloc wrong\n");
			}
			printf("calloc success\n");
		}
	}

	printf("mp_reset_pool\n");

	//测试大块的分配
	for (i = 0;i < 5;i ++) {
		void *l = mp_alloc(p, 8192);
		mp_free(p, l);
	}

	mp_reset_pool(p);

	printf("mp_destory_pool\n");
	for (i = 0;i < 58;i ++) {
		mp_alloc(p, 256);
	}

	mp_destory_pool(p);

	return 0;
}