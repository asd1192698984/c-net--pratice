#include <ngx_http.h>
#include <ngx_config.h>
#include <ngx_core.h>


#define ENABLE_RBTREE	1


static char *ngx_http_pagecount_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_pagecount_init(ngx_conf_t *cf);
static void  *ngx_http_pagecount_create_location_conf(ngx_conf_t *cf);

static ngx_int_t ngx_http_pagecount_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_pagecount_shm_init (ngx_shm_zone_t *zone, void *data);
static void ngx_http_pagecount_rbtree_insert_value(ngx_rbtree_node_t *temp,
        ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);
static ngx_command_t count_commands[]={
    {
        ngx_string("count"),
        NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS, //location指令 不接受任何参数
        ngx_http_pagecount_set, //这个函数将在解析到count指令时被调用
        NGX_HTTP_LOC_CONF_OFFSET, //表示这是一个HTTP location级别的配置指令。
		0, NULL
    },
    ngx_null_command
};
static ngx_http_module_t count_ctx = {
	NULL,
	ngx_http_pagecount_init,
	
	NULL,
	NULL,

	NULL,
	NULL,

	ngx_http_pagecount_create_location_conf,
	NULL,
};
//定义模块 
ngx_module_t ngx_http_pagecount_module_my = {
	NGX_MODULE_V1,
	&count_ctx,
	count_commands,
	NGX_HTTP_MODULE,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NGX_MODULE_V1_PADDING
};

typedef struct{
    int count;
} ngx_http_pagecount_node_t;

typedef struct {

	ngx_rbtree_t rbtree; //红黑树
	ngx_rbtree_node_t sentinel; //每个红黑树都有一个特殊的哨兵节点（sentinel），通常用于表示树的末端 叶子节点的子节点
	
} ngx_http_pagecount_shm_t;

typedef struct{
    ssize_t shmsize; //共享内存大小
    ngx_slab_pool_t *shpool; //共享内存节点
    ngx_http_pagecount_shm_t *sh; //红黑树
}ngx_http_pagecount_conf_t;


ngx_int_t   ngx_http_pagecount_init(ngx_conf_t *cf) {

	return NGX_OK;
}
void  *ngx_http_pagecount_create_location_conf(ngx_conf_t *cf) {

	ngx_http_pagecount_conf_t *conf;
	
	//为结构体分配空间
    //这里创建的conf会传入到count_commands的ngx_http_pagecount_set函数中
	conf = ngx_palloc(cf->pool, sizeof(ngx_http_pagecount_conf_t));
	if (NULL == conf) {
		return NULL;
	}

	conf->shmsize = 0;

	return conf;

}


//这里进行初始化工作
//配置和初始化一个共享内存区域，并将其与一个特定的 NGINX 模块相关联，同时设置该模块的请求处理函数。
//conf 这个参数是指向当前模块的配置结构体的指针
static char *ngx_http_pagecount_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

	ngx_shm_zone_t *shm_zone;	
	ngx_str_t name = ngx_string("pagecount_slab_shm");

	ngx_http_pagecount_conf_t *mconf = (ngx_http_pagecount_conf_t*)conf;
	ngx_http_core_loc_conf_t *corecf;
	
	//ngx_log_error(NGX_LOG_EMERG, cf->log, ngx_errno, "ngx_http_pagecount_set000");
	//分配1M
	mconf->shmsize = 1024*1024;
	
	// 添加共享内存区域 
	shm_zone = ngx_shared_memory_add(cf, &name, mconf->shmsize, &ngx_http_pagecount_module_my);
	if (NULL == shm_zone) {
		return NGX_CONF_ERROR;
	}

	//设置共享内存区域的初始化函数
	shm_zone->init = ngx_http_pagecount_shm_init;
	//将配置结构体 mconf 赋值给共享内存区域的 data 字段
	shm_zone->data = mconf;
	//获取 HTTP 核心模块的位置配置信息
	corecf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
	//设置 HTTP 核心模块的处理函数
	corecf->handler = ngx_http_pagecount_handler;

	return NGX_CONF_OK;
}
//初始化ngx_http_pagecount_conf_t
ngx_int_t ngx_http_pagecount_shm_init (ngx_shm_zone_t *zone, void *data) {

	ngx_http_pagecount_conf_t *conf;
	ngx_http_pagecount_conf_t *oconf = data;

	//拿到自定义结构体对象
	conf = (ngx_http_pagecount_conf_t*)zone->data;
	// 如果 data 参数不为空，表示这是共享内存的重新初始化（如重新加载配置）
	if (oconf) {
		conf->sh = oconf->sh;
		conf->shpool = oconf->shpool;
		return NGX_OK;
	}

	//printf("ngx_http_pagecount_shm_init 0000\n");
	// 第一次初始化共享内存区域 获取slab_pool
	conf->shpool = (ngx_slab_pool_t*)zone->shm.addr;
	// 使用 slab 分配器在共享内存池中分配红黑树结构体
	conf->sh = ngx_slab_alloc(conf->shpool, sizeof(ngx_http_pagecount_shm_t));
	if (conf->sh == NULL) {
		return NGX_ERROR;
	}
	//把slab_pool中的data设为红黑树
	conf->shpool->data = conf->sh;

	//printf("ngx_http_pagecount_shm_init 1111\n");
	
	// 初始化红黑树 传入自定义的插入函数
	ngx_rbtree_init(&conf->sh->rbtree, &conf->sh->sentinel, 
		ngx_http_pagecount_rbtree_insert_value);


	return NGX_OK;

}
// 红黑树的插入函数 
//nginx 提供 ngx_rbtree_insert_value 
static void
ngx_http_pagecount_rbtree_insert_value(ngx_rbtree_node_t *temp,
        ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
   ngx_rbtree_node_t **p;
   //ngx_http_testslab_node_t *lrn, *lrnt;
 
    for (;;)
    {
        if (node->key < temp->key)
        {
            p = &temp->left;
        }
        else if (node->key > temp->key) {
           	p = &temp->right;
        }
        else
        {
          	return ;
        }
 
        if (*p == sentinel)
        {
            break;
        }
 
        temp = *p;
    }
 
    *p = node;
 
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
}

//查找函数需要自己手动实现
static ngx_int_t ngx_http_pagecount_lookup(ngx_http_request_t *r, ngx_http_pagecount_conf_t *conf, ngx_uint_t key) {

    //根节点和哨兵节点
	ngx_rbtree_node_t *node, *sentinel;

	node = conf->sh->rbtree.root;
	sentinel = conf->sh->rbtree.sentinel;

    //记录日志，显示正在查找的键值 key。
	ngx_log_error(NGX_LOG_EMERG, r->connection->log, ngx_errno, " ngx_http_pagecount_lookup 111 --> %x\n", key);
	
	while (node != sentinel) {

		if (key < node->key) {
			node = node->left;
			continue;
		} else if (key > node->key) {
			node = node->right;
			continue;
		} else { // key == node //找到对应的节点 让count++
			node->data ++;
			return NGX_OK;
		}
	}
	
	ngx_log_error(NGX_LOG_EMERG, r->connection->log, ngx_errno, " ngx_http_pagecount_lookup 222 --> %x\n", key);
	
	// insert rbtree
	//没找到的话就执行插入
	
	node = ngx_slab_alloc_locked(conf->shpool, sizeof(ngx_rbtree_node_t));
	if (NULL == node) {
		return NGX_ERROR;
	}

	node->key = key;
	node->data = 1;

    //插入节点
	ngx_rbtree_insert(&conf->sh->rbtree, node);

	ngx_log_error(NGX_LOG_EMERG, r->connection->log, ngx_errno, " insert success\n");
	

	return NGX_OK;
}

//将红黑树中的数据转换为 HTML 格式，以便在网页中显示
static int ngx_encode_http_page_rb(ngx_http_pagecount_conf_t *conf, char *html) {

	sprintf(html, "<h1>Source Insight </h1>");
	strcat(html, "<h2>");

	//使用 ngx_rbtree_min 查找红黑树中的最小节点并将其赋值给 node。
	//ngx_rbtree_traversal(&ngx_pv_tree, ngx_pv_tree.root, ngx_http_count_rbtree_iterator, html);
	ngx_rbtree_node_t *node = ngx_rbtree_min(conf->sh->rbtree.root, conf->sh->rbtree.sentinel);

	do {
		// 声明并初始化两个缓冲区 str 和 buffer。
		char str[INET_ADDRSTRLEN] = {0};
		char buffer[128] = {0};
		//将节点的键（ IP 地址）转换为字符串形式并存储在 str 中
		sprintf(buffer, "req from : %s, count: %d <br/>",
			inet_ntop(AF_INET, &node->key, str, sizeof(str)), node->data);

		strcat(html, buffer);
		// 使用 ngx_rbtree_next 获取红黑树中的下一个节点。
		node = ngx_rbtree_next(&conf->sh->rbtree, node);

	} while (node);
	
	
	strcat(html, "</h2>");

	return NGX_OK;
}

// 处理 HTTP请求，实现页面计数功能。它通过共享内存实现数据的存储与访问，并生成简单的 HTML 响应返回给客户端
static ngx_int_t ngx_http_pagecount_handler(ngx_http_request_t *r) {

	//用于存放生成的HTML内容。
	u_char html[1024] = {0};
	int len = sizeof(html);
	
	ngx_rbtree_key_t key = 0;


	struct sockaddr_in *client_addr =  (struct sockaddr_in*)r->connection->sockaddr;
	
	//使用 ngx_http_get_module_loc_conf 函数获取当前 HTTP 请求的模块配置信息
	ngx_http_pagecount_conf_t *conf = ngx_http_get_module_loc_conf(r, ngx_http_pagecount_module_my);
	//将客户端IP地址的32位表示设置为关键字 key
	key = (ngx_rbtree_key_t)client_addr->sin_addr.s_addr;
	
	//记录日志信息：
	ngx_log_error(NGX_LOG_EMERG, r->connection->log, ngx_errno, " ngx_http_pagecount_handler --> %x\n", key);
	//加锁共享内存：
	ngx_shmtx_lock(&conf->shpool->mutex);
	//调用页面计数模块的查找函数，传递当前请求、配置信息和关键字参数。这个函数可能会在共享内存中更新计数器或其他相关数据。
	ngx_http_pagecount_lookup(r, conf, key);	
	//解锁
	ngx_shmtx_unlock(&conf->shpool->mutex);
	//生成HTTP响应内容
	ngx_encode_http_page_rb(conf, (char*)html);

	//header 设置HTTP响应头部：
	r->headers_out.status = 200;
	ngx_str_set(&r->headers_out.content_type, "text/html");
	ngx_http_send_header(r);

	//body 准备输出缓冲区：
	ngx_buf_t *b = ngx_pcalloc(r->pool,  sizeof(ngx_buf_t));

	ngx_chain_t out;
	out.buf = b;
	out.next = NULL;

	b->pos = html;
	b->last = html+len;
	b->memory = 1;
	b->last_buf = 1;

	return ngx_http_output_filter(r, &out);
	
	
}