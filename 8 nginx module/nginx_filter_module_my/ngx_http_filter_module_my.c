#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
typedef struct {
    //int
    ngx_flag_t enable;
}ngx_http_filter_conf_t;


//定义需要插入的html
static ngx_str_t prefix=ngx_string("<h2>mxl<h2>");

//这两个变量主要是保存原本的nginx的过滤器链条
static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

//实现自己的过滤函数
//中间逻辑自己写 但是结尾记得调用系统的过滤函数 被我们保存在next函数指针中
ngx_int_t ngx_http_mxl_header_filter(ngx_http_request_t *r) {

	if (r->headers_out.status != NGX_HTTP_OK) {
		return ngx_http_next_header_filter(r);
	}

	//r->headers_out.content_type.len == sizeof("text/html")

	r->headers_out.content_length_n += prefix.len;

	return ngx_http_next_header_filter(r);
}
// 自定义的 body 过滤函数。创建一个新的缓冲区，包含 prefix，并将其链入到原有的响应 body 链表中。
// 最后调用原始的 body 过滤函数。
ngx_int_t ngx_http_mxl_body_filter(ngx_http_request_t *r, ngx_chain_t *chain) {
	//创建一个buf
	ngx_buf_t *b = ngx_create_temp_buf(r->pool, prefix.len);
	//设置buf起始位置指针 避免memcpy
	b->start = b->pos = prefix.data;
	//设置结束位置指针
	b->last = b->pos + prefix.len;
	//从内存池中分配一个链的节点
	ngx_chain_t *c1 = ngx_alloc_chain_link(r->pool);
	//头插法
	c1->buf = b;
	c1->next = chain;

	return ngx_http_next_body_filter(r, c1);

}
ngx_int_t  ngx_http_mxl_filter_init(ngx_conf_t *cf){
     ngx_http_next_header_filter = ngx_http_top_header_filter;
	ngx_http_top_header_filter = ngx_http_mxl_header_filter;

	ngx_http_next_body_filter = ngx_http_top_body_filter;
	ngx_http_top_body_filter = ngx_http_mxl_body_filter;
    return NGX_OK;
}

//在解析location时调用 我们需要在这里为location中我们自定义的变量分配空间
void *ngx_http_mxl_filter_create_loc_conf(ngx_conf_t *cf){
    //cf中有内存池 从此分配空间
    ngx_http_filter_conf_t *conf = ngx_palloc(cf->pool, sizeof(ngx_http_filter_conf_t));
    if(conf==NULL) return NULL;

    conf->enable=NGX_CONF_UNSET;
    return conf;
}

//确保模块配置一致性
char*ngx_http_mxl_filter_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child){
    // 将parent和child指针转换为具体的配置结构体类型ngx_http_filter_conf_t。
	ngx_http_filter_conf_t *prev = (ngx_http_filter_conf_t*)parent;
	ngx_http_filter_conf_t *next = (ngx_http_filter_conf_t*)child;
	//这个宏用于合并配置项enable的值
	// 如果next->enable（子级配置）的值为默认值（通常是未设置或无效的值），则使用prev->enable（父级配置）的值。
	// 如果prev->enable（父级配置）的值也未设置，则使用默认值0。
	//printf("ngx_http_zry_filter_merge_loc_conf: %d\n", next->enable);
	ngx_conf_merge_value(next->enable, prev->enable, 0);
    return NGX_CONF_OK;
}
//定义命令组
ngx_command_t ngx_http_mxl_filter_module_cmd[]={
    {
        ngx_string("prefix"),
        //指令 prefix 可以合法地出现在 http、server 和 location 块中。
        //指令接受的参数是布尔值（on 或 off）。
        NGX_HTTP_MAIN_CONF |NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
        ngx_conf_set_flag_slot, //nginx提供的解析函数
        // 用于指示 prefix 指令的值应存储在 loc_conf 级别的配置结构体中。
        NGX_HTTP_LOC_CONF_OFFSET,
        //指示指令的值应存储在 ngx_http_filter_conf_t 结构体的 enable 成员中。
        offsetof(ngx_http_filter_conf_t, enable),
        NULL,
    },
    ngx_null_command
};
//定义一个上下文结构体
static ngx_http_module_t ngx_http_mxl_filter_module_ctx={
    NULL,
    ngx_http_mxl_filter_init,
    NULL,
    NULL,   
    NULL,
    NULL,
    ngx_http_mxl_filter_create_loc_conf,
	ngx_http_mxl_filter_merge_loc_conf,
};
//定义模块名 这里的模块名要和config里的一致 切记
ngx_module_t nginx_filter_module_my={
    NGX_MODULE_V1,
    &ngx_http_mxl_filter_module_ctx,
    ngx_http_mxl_filter_module_cmd,
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