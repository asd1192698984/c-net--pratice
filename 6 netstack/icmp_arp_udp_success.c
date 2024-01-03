

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <sys/poll.h>
#include <arpa/inet.h>


#define NETMAP_WITH_LIBS

#include <net/netmap_user.h> 
#pragma pack(1)



#define ETH_ALEN	6
#define PROTO_IP	0x0800
#define PROTO_ARP	0x0806

#define PROTO_UDP	17
#define PROTO_ICMP	1
#define PROTO_IGMP	2

struct ethhdr {
	unsigned char h_dest[ETH_ALEN];
	unsigned char h_source[ETH_ALEN];
	unsigned short h_proto;
};



struct iphdr {
	unsigned char version;
	unsigned char tos;
	unsigned short tot_len;
	unsigned short id;
	unsigned short flag_off;
	unsigned char ttl;
	unsigned char protocol;
	unsigned short check;
	unsigned int saddr;
	unsigned int daddr;
};


struct udphdr {
	unsigned short source;
	unsigned short dest;
	unsigned short len;
	unsigned short check;
};


struct udppkt {
	struct ethhdr eh;
	struct iphdr ip;
	struct udphdr udp;
	unsigned char body[0];
};



struct arphdr {
	unsigned short h_type;
	unsigned short h_proto;
	unsigned char h_addrlen;
	unsigned char protolen;
	unsigned short oper;
	unsigned char smac[ETH_ALEN];
	unsigned int sip;
	unsigned char dmac[ETH_ALEN];
	unsigned int dip;
};

struct arppkt {
	struct ethhdr eh;
	struct arphdr arp;
};


struct icmphdr {
	unsigned char type;
	unsigned char code;
	unsigned short check;
	unsigned short identifier;
	unsigned short seq;
	unsigned char data[32];
};

struct icmppkt {
	struct ethhdr eh;
	struct iphdr ip;
	struct icmphdr icmp;
};
//数组包含以下字节序列 [0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC]，则调用 print_mac(mac) 将输出 12:34:56:78:9a:bc，以冒号分隔的形式打印MAC地址。
void print_mac(unsigned char *mac) {
	int i = 0;
	//%02x 格式打印每个字节的十六进制值。%02x 表示以两位十六进制数的形式打印，不足两位的数会在前面补零。
	for (i = 0;i < ETH_ALEN-1;i ++) {
		printf("%02x:", mac[i]);
	}
	printf("%02x", mac[i]);
}
//打印IP
void print_ip(unsigned char *ip) {
	int i = 0;

	for (i = 0;i < 3;i ++) {
		printf("%d.", ip[i]);
	}
	printf("%d", ip[i]);
}

//目的mac 源mac 协议
void print_arp(struct arppkt *arp) {
	print_mac(arp->eh.h_dest);
	printf(" ");

	print_mac(arp->eh.h_source);
	printf(" ");

	printf("0x%04x ", ntohs(arp->eh.h_proto));
	printf("  ");
	
}
//函数的主要功能是遍历MAC地址字符串，并将每个字节的十六进制表示转换为对应的二进制形式，存储在mac数组中。具体的操作如下
int str2mac(char *mac, char *str) {

	char *p = str;
	unsigned char value = 0x0;
	int i = 0;

	while (*p != '\0') {
		
		if (*p == ':') {
			mac[i++] = value;
			value = 0x0;
		} else {
			
			unsigned char temp = *p;
			if (temp <= '9' && temp >= '0') {
				temp -= '0';
			} else if (temp <= 'f' && temp >= 'a') {
				temp -= 'a';
				temp += 10;
			} else if (temp <= 'F' && temp >= 'A') {
				temp -= 'A';
				temp += 10;
			} else {	
				break;
			}
			value <<= 4;
			value |= temp;
		}
		p ++;
	}

	mac[i] = value;

	return 0;
}

//这是一个名为echo_arp_pkt的函数定义，用于构造回显ARP数据包。函数接受三个参数：arp是指向原始ARP数据包的指针，arp_rt是指向要构造的回显ARP数据包的指针，hmac是一个指向MAC地址字符串的指针。
void echo_arp_pkt(struct arppkt *arp, struct arppkt *arp_rt, char *hmac) {
	//将原始ARP数据包的内容复制到新的ARP数据包结构体arp_rt中。
	memcpy(arp_rt, arp, sizeof(struct arppkt));
	//将新ARP数据包的目标MAC地址设置为原始ARP数据包的源MAC地址。
	memcpy(arp_rt->eh.h_dest, arp->eh.h_source, ETH_ALEN);
	//使用自定义函数str2mac将新ARP数据包的源MAC地址设置为指定的MAC地址。
	str2mac(arp_rt->eh.h_source, hmac);
	arp_rt->eh.h_proto = arp->eh.h_proto;
	//将新ARP数据包的硬件地址长度设置为6（通常是MAC地址的长度）
	arp_rt->arp.h_addrlen = 6;
	//将新ARP数据包的协议地址长度设置为4（通常是IPv4地址的长度）
	arp_rt->arp.protolen = 4;
	//将新ARP数据包的操作码设置为2，表示ARP回应。
	arp_rt->arp.oper = htons(2);
	//使用自定义函数str2mac将新ARP数据包的源MAC地址设置为指定的MAC地址。
	str2mac(arp_rt->arp.smac, hmac);
	//将新ARP数据包的源IP地址设置为原始ARP数据包的目标IP地址。
	arp_rt->arp.sip = arp->arp.dip;
	//将新ARP数据包的目标MAC地址设置为原始ARP数据包的源MAC地址。
	memcpy(arp_rt->arp.dmac, arp->arp.smac, ETH_ALEN);
	//将新ARP数据包的目标IP地址设置为原始ARP数据包的源IP地址。
	arp_rt->arp.dip = arp->arp.sip;

}


void echo_udp_pkt(struct udppkt *udp, struct udppkt *udp_rt) {

	//将原始UDP数据包 udp 的内容拷贝到回显UDP数据包 udp_rt 中，确保两者具有相同的数据。
	memcpy(udp_rt, udp, sizeof(struct udppkt));
	//原始数据包的源MAC地址拷贝到回显数据包的目标MAC地址字段中
	memcpy(udp_rt->eh.h_dest, udp->eh.h_source, ETH_ALEN);
	//将原始数据包的目标MAC地址拷贝到回显数据包的源MAC地址字段中
	memcpy(udp_rt->eh.h_source, udp->eh.h_dest, ETH_ALEN);
	//通过交换源IP地址和目标IP地址
	udp_rt->ip.saddr = udp->ip.daddr;
	udp_rt->ip.daddr = udp->ip.saddr;
	//以及交换源端口号和目标端口号
	udp_rt->udp.source = udp->udp.dest;
	udp_rt->udp.dest = udp->udp.source;

}
//计算校验和
unsigned short in_cksum(unsigned short *addr, int len)
{
	register int nleft = len;
	register unsigned short *w = addr;
	register int sum = 0;
	unsigned short answer = 0;

	while (nleft > 1)  {
		sum += *w++;
		nleft -= 2;
	}

	if (nleft == 1) {
		*(u_char *)(&answer) = *(u_char *)w ;
		sum += answer;
	}

	sum = (sum >> 16) + (sum & 0xffff);	
	sum += (sum >> 16);			
	answer = ~sum;
	
	return (answer);

}


void echo_icmp_pkt(struct icmppkt *icmp, struct icmppkt *icmp_rt) {

	memcpy(icmp_rt, icmp, sizeof(struct icmppkt));

	icmp_rt->icmp.type = 0x0; //
	icmp_rt->icmp.code = 0x0; //
	icmp_rt->icmp.check = 0x0;

	icmp_rt->ip.saddr = icmp->ip.daddr;
	icmp_rt->ip.daddr = icmp->ip.saddr;

	memcpy(icmp_rt->eh.h_dest, icmp->eh.h_source, ETH_ALEN);
	memcpy(icmp_rt->eh.h_source, icmp->eh.h_dest, ETH_ALEN);

	icmp_rt->icmp.check = in_cksum((unsigned short*)&icmp_rt->icmp, sizeof(struct icmphdr));
	
}


int main() {
	
	struct ethhdr *eh;
	struct pollfd pfd = {0};
	struct nm_pkthdr h;
	unsigned char *stream = NULL;

	//打开netmap设备：使用nm_open函数打开netmap设备，这里是"netmap:eth0"，表示打开名为eth0的网络接口。
	struct nm_desc *nmr = nm_open("netmap:eth0", NULL, 0, NULL);
	if (nmr == NULL) {
		return -1;
	}

	pfd.fd = nmr->fd;
	pfd.events = POLLIN;

	while (1) {
		//设置轮询事件：使用poll函数设置一个文件描述符的轮询事件，以等待数据包的到达。
		int ret = poll(&pfd, 1, -1);
		if (ret < 0) continue;
		
		if (pfd.revents & POLLIN) {
			//接收数据包：使用nm_nextpkt函数从netmap设备中接收下一个数据包，并将其存储在stream指针中。
			stream = nm_nextpkt(nmr, &h);
			// 解析以太网头部：将stream指针强制转换为以太网头部结构体ethhdr，以便进一步解析数据包。
			eh = (struct ethhdr*)stream;
			//通过检查以太网头部中的协议字段，判断数据包的协议类型。
			if (ntohs(eh->h_proto) == PROTO_IP) {

				struct udppkt *udp = (struct udppkt*)stream;
				if (udp->ip.protocol == PROTO_UDP) {

					struct in_addr addr;
					addr.s_addr = udp->ip.saddr;

					int udp_length = ntohs(udp->udp.len);
					//于将32位的IPv4地址转换为点分十进制字符串表示的函数
					printf("%s:%d:length:%d, ip_len:%d --> ", inet_ntoa(addr), udp->udp.source, 
						udp_length, ntohs(udp->ip.tot_len));

					udp->body[udp_length-8] = '\0';
					printf("udp --> %s\n", udp->body);
#if 1	
					struct udppkt udp_rt;
					echo_udp_pkt(udp, &udp_rt);
					nm_inject(nmr, &udp_rt, sizeof(struct udppkt));
#endif
				} else if (udp->ip.protocol == PROTO_ICMP) {  //发送ICMP包
					
					struct icmppkt *icmp = (struct icmppkt*)stream;

					printf("icmp ---------- --> %d, %x\n", icmp->icmp.type, icmp->icmp.check);
					if (icmp->icmp.type == 0x08) {
						struct icmppkt icmp_rt = {0};
						echo_icmp_pkt(icmp, &icmp_rt);

						//printf("icmp check %x\n", icmp_rt.icmp.check);
						nm_inject(nmr, &icmp_rt, sizeof(struct icmppkt));
					}
					
				} else if (udp->ip.protocol == PROTO_IGMP) {

				} else {
					printf("other ip packet");
				}
				
			}  else if (ntohs(eh->h_proto) == PROTO_ARP) { //如果是arp报文

				struct arppkt *arp = (struct arppkt *)stream;
				struct arppkt arp_rt;
				// 函数将 IPv4 地址字符串转换为 32 位无符号整数时，返回的整数值是以网络字节序（大端字节序）表示的。
				if (arp->arp.dip == inet_addr("192.168.82.168")) {
					echo_arp_pkt(arp, &arp_rt, "00:0c:29:fb:c6:6b");
					nm_inject(nmr, &arp_rt, sizeof(struct arppkt));
				}
			}
		} 
	}
}


