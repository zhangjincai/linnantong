#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include "lib_general.h"
#include "device.h"
#include "lib_lnt.h"
#include "lib_crypto.h"

#include <fcntl.h>


/*
 * test by zjc
 */
#define CONFS_USING_TEST_BY_ZJC		0 //zjc测试用

/*
 * 读卡器固件升级功能开启与否 1:开启 0:关闭
 */
#define CONFS_USING_READER_UPDATE	0 


#define	LNT_CONSUME_TRY_TIMES	2 //消费次数上限

//#define LNT_EXE_DEBUG			//执行调试

//#define LNT_REG_PROC_DEBUG			//注册
//#define LNT_MEMBER_PROC_DEBUG	
//#define LNT_ON_CARD_PROC_DEBUG	//开卡
//#define LNT_OFF_CARD_PROC_DEBUG	//销卡
//#define LNT_RECHARGE_PROC_DEBUG	//充值

#define LNT_USING_SELECT_RD			1  //网络读方法
   

#define LNT_FW_VER				"V01T16022301"
#define LNT_REG_CTE_SLEEP		(15) //15
#define LNT_SW_9000				LNT_NTOHS(0x9000)


#define __msleep(x) 				usleep(x*1000)


#define USER_ID 			"test11" //add by zjc 2016-03-17
#define USE_NEW_USER_ID 	1
#define UID_LEN				(strlen(USER_ID))

/*
 * 签到数据索引
 */
enum A_REG_IDX
{
	A_REG_IDX_RETCODE = 12,
	A_REG_IDX_LEN = 13,
	A_REG_IDX_DATA	= 14
};


typedef struct lnt_notify
{
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	unsigned int notify;		
}lnt_notify_t;

typedef struct lnt_notify_ptr
{
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
	unsigned int notify;		
}lnt_notify_ptr_t;


static lib_lnt_config_t g_lnt_conf;    //配置
static unsigned int g_lnt_register_stat = 0;   //岭南通签到状态
static unsigned int g_lnt_register_proc_code = 0;  //岭南通签到流程
//static pthread_mutex_t g_register_proc_mutex = PTHREAD_MUTEX_INITIALIZER;  //岭南通签到流程
//static lnt_notify_t g_lnt_register_notify = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, LNT_REGISTER_STAT_INIT}; //岭南通签到事件

pthread_mutex_t *g_register_proc_mutex;  //岭南通签到流程

lnt_notify_ptr_t *g_lnt_register_notify_ptr;  




static int __tcp_connect_nonb(const char *ipaddr, const unsigned short port, const unsigned int nsec)	
{
#if 0
	int sockfd;
	int ret = 0, flags = 0, error = 0;
	struct sockaddr_in serv_addr;
	fd_set rset, wset;
	struct timeval tval;
	socklen_t len;

	memset(&serv_addr, 0, sizeof(struct sockaddr_in));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = inet_addr(ipaddr);
		
	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sockfd < 0)
		goto ERR;

	flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd,F_SETFL,flags | O_NONBLOCK);		
	
	ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr));
	if(ret < 0)
	{
		if(errno != EINPROGRESS) 
			goto Done;
	}

	if(ret == 0)
		goto Done;

	FD_ZERO(&rset);
	FD_SET(sockfd, &rset);
	wset = rset;
	tval.tv_sec = nsec;
	tval.tv_usec = 0;
	
	/* 0: timeout -1:error other: realy count */
	if((ret = select(sockfd + 1, &rset, &wset, NULL, nsec ? &tval : NULL)) == 0)
	{
		close(sockfd);
		errno = ETIMEDOUT;
		goto Done;
	}
	
	if(FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset))
	{
		len = sizeof(error);
		if(getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
			goto Done;
	}
	
Done:
	fcntl(sockfd, F_SETFL, flags);
	if(error) /* linux error always return  0 */
	{
		close(sockfd);
		errno = error;
		return -1;
	}
	return sockfd;

ERR:
	return -1;
#else
	int client_fd = -1;  
    struct sockaddr_in server_addr;  
    socklen_t socklen = sizeof(server_addr);  
  
    if((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)  
    {  
        printf("---------create socket error!\n");  
        return -1;
    }  

	printf("==============socketfd:%d...\n", client_fd); 
	
    memset(&server_addr, 0, sizeof(server_addr));  
    server_addr.sin_family = AF_INET;  
	server_addr.sin_addr.s_addr = inet_addr(ipaddr);
    server_addr.sin_port = htons(port);  
  
    if(connect(client_fd, (struct sockaddr*)&server_addr, socklen) < 0)  
    {  
        printf("=============%s\n", strerror(errno));  
        return -1;
    }  

	return client_fd;
#endif
}

static int __lnt_tcp_client_nonb_new(const char *ipaddr, const unsigned short port, const unsigned int nsec)
{
	return __tcp_connect_nonb(ipaddr, port, nsec);
}


static int __lnt_tcp_client_new(const char *ipaddr, const unsigned short port)
{

	if(( NULL == ipaddr) || (port <= 0))
		return -1;
	
	int client_fd = -1;  
    struct sockaddr_in server_addr;  
    socklen_t socklen = sizeof(server_addr);  
  
    if((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)  
    {  
        printf("---------create socket error!\n");  

		return -1;
    }  

    memset(&server_addr, 0, sizeof(server_addr));  
    server_addr.sin_family = AF_INET;  
	server_addr.sin_addr.s_addr = inet_addr(ipaddr);
    server_addr.sin_port = htons(port);  
  
    if(connect(client_fd, (struct sockaddr*)&server_addr, socklen) < 0)  
    {  
        printf("=============connect:%s\n", strerror(errno));  

		close(client_fd);
		return -1;
    }  
	
	return client_fd;
}










void *__lnt_register_entry(void *arg);

enum AGENT_WAY
{
	AGENT_WAY_REG = 0,
	AGENT_WAY_COM = 1
};

struct agent_pkt_ptr
{
	char *exp_ptr;      //要解释数据
	int const_exp_sz;  //要解释数据长度
	
	char *data_ptr;   //返回的信息字段
	int data_sz;   //返回信息长度字段
	
	char *extra_ptr;   //额外数据
	int extra_sz; //额外数据长度
};


static unsigned char g_agent_data[2][1024] ={{0},{0}};  //代理报文
static unsigned int g_agent_now_len[2];  //报文长度
static unsigned int g_agent_next_idx[2]; //报文索引
//static unsigned char g_agent_cur_sn = 0;  //报文流水号

static unsigned   long   g_crc32_table[256]   =   {   
		0x00000000L,   0x77073096L,   0xee0e612cL,   0x990951baL,   0x076dc419L,   
		0x706af48fL,   0xe963a535L,   0x9e6495a3L,   0x0edb8832L,   0x79dcb8a4L,   
		0xe0d5e91eL,   0x97d2d988L,   0x09b64c2bL,   0x7eb17cbdL,   0xe7b82d07L,   
		0x90bf1d91L,   0x1db71064L,   0x6ab020f2L,   0xf3b97148L,   0x84be41deL,   
		0x1adad47dL,   0x6ddde4ebL,   0xf4d4b551L,   0x83d385c7L,   0x136c9856L,   
		0x646ba8c0L,   0xfd62f97aL,   0x8a65c9ecL,   0x14015c4fL,   0x63066cd9L,   
		0xfa0f3d63L,   0x8d080df5L,   0x3b6e20c8L,   0x4c69105eL,   0xd56041e4L,   
		0xa2677172L,   0x3c03e4d1L,   0x4b04d447L,   0xd20d85fdL,   0xa50ab56bL,   
		0x35b5a8faL,   0x42b2986cL,   0xdbbbc9d6L,   0xacbcf940L,   0x32d86ce3L,   
		0x45df5c75L,   0xdcd60dcfL,   0xabd13d59L,   0x26d930acL,   0x51de003aL,   
		0xc8d75180L,   0xbfd06116L,   0x21b4f4b5L,   0x56b3c423L,   0xcfba9599L,   
		0xb8bda50fL,   0x2802b89eL,   0x5f058808L,   0xc60cd9b2L,   0xb10be924L,   
		0x2f6f7c87L,   0x58684c11L,   0xc1611dabL,   0xb6662d3dL,   0x76dc4190L,   
		0x01db7106L,   0x98d220bcL,   0xefd5102aL,   0x71b18589L,   0x06b6b51fL,   
		0x9fbfe4a5L,   0xe8b8d433L,   0x7807c9a2L,   0x0f00f934L,   0x9609a88eL,   
		0xe10e9818L,   0x7f6a0dbbL,   0x086d3d2dL,   0x91646c97L,   0xe6635c01L,   
		0x6b6b51f4L,   0x1c6c6162L,   0x856530d8L,   0xf262004eL,   0x6c0695edL,   
		0x1b01a57bL,   0x8208f4c1L,   0xf50fc457L,   0x65b0d9c6L,   0x12b7e950L,   
		0x8bbeb8eaL,   0xfcb9887cL,   0x62dd1ddfL,   0x15da2d49L,   0x8cd37cf3L,   
		0xfbd44c65L,   0x4db26158L,   0x3ab551ceL,   0xa3bc0074L,   0xd4bb30e2L,   
		0x4adfa541L,   0x3dd895d7L,   0xa4d1c46dL,   0xd3d6f4fbL,   0x4369e96aL,   
		0x346ed9fcL,   0xad678846L,   0xda60b8d0L,   0x44042d73L,   0x33031de5L,   
		0xaa0a4c5fL,   0xdd0d7cc9L,   0x5005713cL,   0x270241aaL,   0xbe0b1010L,   
		0xc90c2086L,   0x5768b525L,   0x206f85b3L,   0xb966d409L,   0xce61e49fL,   
		0x5edef90eL,   0x29d9c998L,   0xb0d09822L,   0xc7d7a8b4L,   0x59b33d17L,   
		0x2eb40d81L,   0xb7bd5c3bL,   0xc0ba6cadL,   0xedb88320L,   0x9abfb3b6L,   
		0x03b6e20cL,   0x74b1d29aL,   0xead54739L,   0x9dd277afL,   0x04db2615L,   
		0x73dc1683L,   0xe3630b12L,   0x94643b84L,   0x0d6d6a3eL,   0x7a6a5aa8L,   
		0xe40ecf0bL,   0x9309ff9dL,   0x0a00ae27L,   0x7d079eb1L,   0xf00f9344L,   
		0x8708a3d2L,   0x1e01f268L,   0x6906c2feL,   0xf762575dL,   0x806567cbL,   
		0x196c3671L,   0x6e6b06e7L,   0xfed41b76L,   0x89d32be0L,   0x10da7a5aL,   
		0x67dd4accL,   0xf9b9df6fL,   0x8ebeeff9L,   0x17b7be43L,   0x60b08ed5L,   
		0xd6d6a3e8L,   0xa1d1937eL,   0x38d8c2c4L,   0x4fdff252L,   0xd1bb67f1L,   
		0xa6bc5767L,   0x3fb506ddL,   0x48b2364bL,   0xd80d2bdaL,   0xaf0a1b4cL,   
		0x36034af6L,   0x41047a60L,   0xdf60efc3L,   0xa867df55L,   0x316e8eefL,   
		0x4669be79L,   0xcb61b38cL,   0xbc66831aL,   0x256fd2a0L,   0x5268e236L,   
		0xcc0c7795L,   0xbb0b4703L,   0x220216b9L,   0x5505262fL,   0xc5ba3bbeL,   
		0xb2bd0b28L,   0x2bb45a92L,   0x5cb36a04L,   0xc2d7ffa7L,   0xb5d0cf31L,   
		0x2cd99e8bL,   0x5bdeae1dL,   0x9b64c2b0L,   0xec63f226L,   0x756aa39cL,   
		0x026d930aL,   0x9c0906a9L,   0xeb0e363fL,   0x72076785L,   0x05005713L,   
		0x95bf4a82L,   0xe2b87a14L,   0x7bb12baeL,   0x0cb61b38L,   0x92d28e9bL,   
		0xe5d5be0dL,   0x7cdcefb7L,   0x0bdbdf21L,   0x86d3d2d4L,   0xf1d4e242L,   
		0x68ddb3f8L,   0x1fda836eL,   0x81be16cdL,   0xf6b9265bL,   0x6fb077e1L,   
		0x18b74777L,   0x88085ae6L,   0xff0f6a70L,   0x66063bcaL,   0x11010b5cL,   
		0x8f659effL,   0xf862ae69L,   0x616bffd3L,   0x166ccf45L,   0xa00ae278L,   
		0xd70dd2eeL,   0x4e048354L,   0x3903b3c2L,   0xa7672661L,   0xd06016f7L,   
		0x4969474dL,   0x3e6e77dbL,   0xaed16a4aL,   0xd9d65adcL,   0x40df0b66L,   
		0x37d83bf0L,   0xa9bcae53L,   0xdebb9ec5L,   0x47b2cf7fL,   0x30b5ffe9L,   
		0xbdbdf21cL,   0xcabac28aL,   0x53b39330L,   0x24b4a3a6L,   0xbad03605L,   
		0xcdd70693L,   0x54de5729L,   0x23d967bfL,   0xb3667a2eL,   0xc4614ab8L,   
		0x5d681b02L,   0x2a6f2b94L,   0xb40bbe37L,   0xc30c8ea1L,   0x5a05df1bL,   
		0x2d02ef8dL   
}; 

static unsigned int __crc32(unsigned char *buffer, unsigned int size)
{
	  unsigned int crc = 0xffffffffL;
	 
	    while (size--)
	    { 
	        crc = g_crc32_table[(crc ^ *(buffer++)) & 0xFFU] ^ (( crc >> 8) & 0x00FFFFFFL); 
	    } 
		
	    return (crc^0xffffffffL); 	
}

unsigned int lnt_crc32( unsigned char *buffer, unsigned int size)
{
	return  __crc32(buffer, size);
}

#if 0
static unsigned char __agent_pkt_get_sn(void)
{
	g_agent_cur_sn++;
	if(g_agent_cur_sn > 0xff)
		g_agent_cur_sn = 1;

	return g_agent_cur_sn;
}
#endif

static int __lnt_net_read(int sockfd, void *buff, const unsigned int len, const unsigned int msec)
{
	if(buff == NULL)
		return -1;

	int ret = -1;
	int s_sec = msec /1000;
	int i;

	fprintf(stderr, "+++++++++++++++++s_sec=%d\n", s_sec);
	
	for(i = 0; i < s_sec; i++)
	{
		

		lib_msleep(400);

		ret = read(sockfd, buff, len);
		if(ret > 0)
		{
			fprintf(stderr, "***********************ret=%d\n", ret);
			return ret;
		}
		

		lib_msleep(400);

		fprintf(stderr, "--------------------i=%d\n", i);
	}
	
	return -2;  //超时
}


static void __agent_pkt_hd_set(enum AGENT_WAY way, const unsigned short cmd, const unsigned char attr)
{
	lnt_agent_packet_head_t agent_pkt_head;

	memset(&g_agent_data[way], 0, sizeof(g_agent_data[way]));
	g_agent_now_len[way] = 0;
	g_agent_next_idx[way] = 0;
	
	memset(&agent_pkt_head, 0, sizeof(lnt_agent_packet_head_t));
	agent_pkt_head.head = LNT_HTONS(AGENT_HEADER);
	agent_pkt_head.type = AGENT_LNT;
	agent_pkt_head.cmd = LNT_HTONS(cmd);
	agent_pkt_head.sn = 0;
	agent_pkt_head.attr = attr;
	agent_pkt_head.length = 0;

	memcpy(&(g_agent_data[way][0]), &agent_pkt_head, sizeof(lnt_agent_packet_head_t));
	g_agent_now_len[way] += sizeof(lnt_agent_packet_head_t);
	g_agent_next_idx[way] += sizeof(lnt_agent_packet_head_t);
}

/*-----------------------------开卡报文头(第三方扣费)------------------------*/ //add by zjc
static void __agent_pkt_hd_fill(enum AGENT_WAY way, const unsigned short cmd, const unsigned char attr)
{
	lnt_agent_packet_head_t agent_pkt_head;

	memset(&g_agent_data[way], 0, sizeof(g_agent_data[way]));
	g_agent_now_len[way] = 0;
	g_agent_next_idx[way] = 0;
	
	memset(&agent_pkt_head, 0, sizeof(lnt_agent_packet_head_t));
	agent_pkt_head.head = LNT_HTONS(AGENT_HEADER);
	agent_pkt_head.type = AGENT_OTHER_CONSUME;
	agent_pkt_head.cmd = LNT_HTONS(cmd);
	agent_pkt_head.sn = 0;
	agent_pkt_head.attr = attr;
	agent_pkt_head.length = 0;

	memcpy(&(g_agent_data[way][0]), &agent_pkt_head, sizeof(lnt_agent_packet_head_t));
	g_agent_now_len[way] += sizeof(lnt_agent_packet_head_t);
	g_agent_next_idx[way] += sizeof(lnt_agent_packet_head_t);
}


/*-----------------------------充值报文头------------------------*/ //add by zjc
static void __agent_pkt_hd_setup(enum AGENT_WAY way, const unsigned short cmd, const unsigned char attr)
{
	lnt_agent_packet_head_t agent_pkt_head;

	memset(&g_agent_data[way], 0, sizeof(g_agent_data[way]));
	g_agent_now_len[way] = 0;
	g_agent_next_idx[way] = 0;
	
	memset(&agent_pkt_head, 0, sizeof(lnt_agent_packet_head_t));
	agent_pkt_head.head = LNT_HTONS(AGENT_HEADER);
	agent_pkt_head.type = AGENT_RECHARGE;
	agent_pkt_head.cmd = LNT_HTONS(cmd);
	agent_pkt_head.sn = 0;
	agent_pkt_head.attr = attr;
	agent_pkt_head.length = 0;

	memcpy(&(g_agent_data[way][0]), &agent_pkt_head, sizeof(lnt_agent_packet_head_t));
	g_agent_now_len[way] += sizeof(lnt_agent_packet_head_t);
	g_agent_next_idx[way] += sizeof(lnt_agent_packet_head_t);
}


static void __agent_pkt_set_sn(enum AGENT_WAY way, const unsigned int sn)
{
	unsigned int to_sn = LNT_HTONL(sn);
	memcpy(&(g_agent_data[way][5]), &to_sn, 4);	 //第5字节是报文序列号
}

static void __agent_pkt_char_set(enum AGENT_WAY way, const unsigned char ch)
{
	g_agent_data[way][g_agent_next_idx[way]] = ch;
	g_agent_now_len[way] += 1;
	g_agent_next_idx[way] += 1;
}

static void __agent_pkt_data_set(enum AGENT_WAY way, const void *ptr, const unsigned int size)
{
	memcpy(&(g_agent_data[way][g_agent_next_idx[way]]), ptr, size);
	g_agent_now_len[way] += size;
	g_agent_next_idx[way] += size;
}

static void __agent_pkt_crc32_set(enum AGENT_WAY way)
{
	unsigned short length = LNT_HTONS(g_agent_now_len[way] - 12); //报文头长度为12
	memcpy(&(g_agent_data[way][10]), &length, 2); //第10字节为报文长度
	
	unsigned int crc32 = LNT_HTONL(__crc32(&(g_agent_data[way][0]), g_agent_now_len[way]));
	memcpy(&(g_agent_data[way][g_agent_next_idx[way]]), &crc32, 4);
	
	g_agent_now_len[way] += 4;
	g_agent_next_idx[way] += 4;
}

static unsigned int __agent_pkt_data_get(enum AGENT_WAY way, void *ptr)
{
	memcpy(ptr, &(g_agent_data[way][0]), g_agent_now_len[way]);
	return  g_agent_now_len[way];
}

/*
 * 签到数据解析
 */
static enum AGENT_ERR __agent_pkt_data_register_explain(struct agent_pkt_ptr *agent_ptr)
{
	if(agent_ptr == NULL)
		goto Done;

	unsigned int crc32 = 0;
	lnt_agent_packet_head_t agent_pkt_head;
	int agent_pkt_len = sizeof(lnt_agent_packet_head_t);
	unsigned int calc_crc32 = lnt_crc32((unsigned char *)agent_ptr->exp_ptr, agent_ptr->const_exp_sz - 4);
	memcpy(&crc32, &(agent_ptr->exp_ptr[agent_ptr->const_exp_sz - 4]), 4);

	if(calc_crc32 != LNT_NTOHL(crc32))  //检查报文CRC
		return AGENT_ERR_PKT_CRC;

	if(agent_ptr->const_exp_sz < agent_pkt_len) //判断数据长度是否小于协议结构要求长度
	{
		fprintf(stderr, "agent_ptr->const_exp_sz < agent_pkt_len\n");
		goto Done;
	}
	
	memset(&agent_pkt_head, 0, agent_pkt_len);
	memcpy(&agent_pkt_head, agent_ptr->exp_ptr, agent_pkt_len);

	if(LNT_NTOHS(agent_pkt_head.head) != AGENT_HEADER) //判断协议头
	{
		fprintf(stderr, "LNT_NTOHL(agent_pkt_head.head) != AGENT_HEADER\n");
		goto Done;
	}
	
	if(agent_pkt_head.type != AGENT_LNT)  //判断类型
	{
		fprintf(stderr, "agent_pkt_head.type != AGENT_LNT\n");
		goto Done;
	}
	
	if(agent_ptr->exp_ptr[A_REG_IDX_RETCODE] != AGENT_ERR_OK) //信息返回码
	{
		fprintf(stderr, "agent_ptr->exp_ptr[A_REG_IDX_RETCODE] != AGENT_ERR_OK,  0x%02x\n", agent_ptr->exp_ptr[A_REG_IDX_RETCODE]);
		return agent_ptr->exp_ptr[A_REG_IDX_RETCODE];
	}

	if(agent_ptr->exp_ptr[A_REG_IDX_LEN] != 0) //数据长度
	{
		memcpy(agent_ptr->data_ptr, &(agent_ptr->exp_ptr[A_REG_IDX_DATA]), agent_ptr->data_sz);  //拷贝数据
		agent_ptr->data_sz = agent_ptr->exp_ptr[A_REG_IDX_LEN];
	}
	else
		agent_ptr->data_sz = 0;

	fprintf(stderr, "Agent Pkt Data Register Explain OK\n");
	return AGENT_ERR_OK;

Done:
	fprintf(stderr, "Agent Pkt Data Explain Register ERR UNKNOWN!\n");
	return AGENT_ERR_UNKNOWN;
}


/*
 * 开卡数据解析
 */
static enum AGENT_ERR __agent_pkt_data_on_card_explain(struct agent_pkt_ptr *agent_ptr)
{
	if(agent_ptr == NULL)
		goto Done;

	unsigned int crc32 = 0;
	lnt_agent_packet_head_t agent_pkt_head;
	int agent_pkt_len = sizeof(lnt_agent_packet_head_t);
	unsigned int calc_crc32 = lnt_crc32((unsigned char *)agent_ptr->exp_ptr, agent_ptr->const_exp_sz - 4);
	memcpy(&crc32, &(agent_ptr->exp_ptr[agent_ptr->const_exp_sz - 4]), 4);

	if(calc_crc32 != LNT_NTOHL(crc32))  //检查报文CRC
		return AGENT_ERR_PKT_CRC;

	if(agent_ptr->const_exp_sz < agent_pkt_len) //判断数据长度是否小于协议结构要求长度
	{
		fprintf(stderr, "agent_ptr->const_exp_sz < agent_pkt_len\n");
		goto Done;
	}
	
	memset(&agent_pkt_head, 0, agent_pkt_len);
	memcpy(&agent_pkt_head, agent_ptr->exp_ptr, agent_pkt_len);

	if(LNT_NTOHS(agent_pkt_head.head) != AGENT_HEADER) //判断协议头
	{
		fprintf(stderr, "LNT_NTOHL(agent_pkt_head.head) != AGENT_HEADER\n");
		goto Done;
	}
	
	if(agent_pkt_head.type != AGENT_LNT)  //判断类型
	{
		fprintf(stderr, "agent_pkt_head.type != AGENT_LNT\n");
		goto Done;
	}
	
	memcpy(agent_ptr->data_ptr, &(agent_ptr->exp_ptr[12]), agent_ptr->data_sz);  //拷贝数据 第12字节是数据体
	agent_ptr->data_sz = LNT_NTOHS(agent_pkt_head.length); //数据长度

	
	fprintf(stderr, "Agent Pkt Data On Card Explain OK\n");

	if(LNT_NTOHS(agent_pkt_head.cmd) == AGENT_ON_CARD_STAGE2)
	{
		g_lnt_conf.pkt_sn_RO = LNT_NTOHL(agent_pkt_head.sn); //保存代理服务器返回的报文序列号
	}

	/* 
	 *阶段是4  交易记录上报应答
	 *阶段是8  开卡结果上报应答
	 */
	if((LNT_NTOHS(agent_pkt_head.cmd) == AGENT_ON_CARD_STAGE4) || (LNT_NTOHS(agent_pkt_head.cmd) == AGENT_ON_CARD_STAGE8))  
		return AGENT_ERR_OK;

	return (unsigned char)agent_ptr->data_ptr[8]; 

Done:
	fprintf(stderr, "Agent Pkt Data Explain On Card ERR UNKNOWN!\n");
	return AGENT_ERR_UNKNOWN;
}

/*
 * 开卡数据解析（第三方扣费）
 */
static enum AGENT_ERR __agent_pkt_data_on_card_otherConsume_explain(struct agent_pkt_ptr *agent_ptr)
{
	if(agent_ptr == NULL)
		goto Done;

	unsigned int crc32 = 0;
	lnt_agent_packet_head_t agent_pkt_head;
	int agent_pkt_len = sizeof(lnt_agent_packet_head_t);
	unsigned int calc_crc32 = lnt_crc32((unsigned char *)agent_ptr->exp_ptr, agent_ptr->const_exp_sz - 4);
	memcpy(&crc32, &(agent_ptr->exp_ptr[agent_ptr->const_exp_sz - 4]), 4);

	if(calc_crc32 != LNT_NTOHL(crc32))  //检查报文CRC
		return AGENT_ERR_PKT_CRC;

	if(agent_ptr->const_exp_sz < agent_pkt_len) //判断数据长度是否小于协议结构要求长度
	{
		fprintf(stderr, "agent_ptr->const_exp_sz < agent_pkt_len\n");
		goto Done;
	}
	
	memset(&agent_pkt_head, 0, agent_pkt_len);
	memcpy(&agent_pkt_head, agent_ptr->exp_ptr, agent_pkt_len);

	if(LNT_NTOHS(agent_pkt_head.head) != AGENT_HEADER) //判断协议头
	{
		fprintf(stderr, "LNT_NTOHL(agent_pkt_head.head) != AGENT_HEADER\n");
		goto Done;
	}
	
	if(agent_pkt_head.type != AGENT_OTHER_CONSUME)  //判断类型
	{
		fprintf(stderr, "agent_pkt_head.type != AGENT_OTHER_CONSUME\n");
		goto Done;
	}
	
	memcpy(agent_ptr->data_ptr, &(agent_ptr->exp_ptr[12]), agent_ptr->data_sz);  //拷贝数据 第12字节是数据体
	agent_ptr->data_sz = LNT_NTOHS(agent_pkt_head.length); //数据长度

	
	fprintf(stderr, "Agent Pkt Data On Card OtherConsume Explain OK\n");

	if(LNT_NTOHS(agent_pkt_head.cmd) == AGENT_ON_CARD_OTHER_CONSUME_STAGE2)
	{
		g_lnt_conf.pkt_sn_RO = LNT_NTOHL(agent_pkt_head.sn); //保存代理服务器返回的报文序列号
	}

	/* 
	 *阶段是8  开卡结果上报应答
	 */
	if(LNT_NTOHS(agent_pkt_head.cmd) == AGENT_ON_CARD_OTHER_CONSUME_STAGE6)  
		return AGENT_ERR_OK;

	return (unsigned char)agent_ptr->data_ptr[8]; 

Done:
	fprintf(stderr, "Agent Pkt Data Explain On Card OtherConsume ERR UNKNOWN!\n");
	return AGENT_ERR_UNKNOWN;
}

/*
 * 销卡数据解析
 */
static enum AGENT_ERR __agent_pkt_data_off_card_explain(struct agent_pkt_ptr *agent_ptr)
{
	if(agent_ptr == NULL)
		goto Done;

	unsigned int crc32 = 0;
	lnt_agent_packet_head_t agent_pkt_head;
	int agent_pkt_len = sizeof(lnt_agent_packet_head_t);
	unsigned int calc_crc32 = lnt_crc32((unsigned char *)agent_ptr->exp_ptr, agent_ptr->const_exp_sz - 4);
	memcpy(&crc32, &(agent_ptr->exp_ptr[agent_ptr->const_exp_sz - 4]), 4);

	if(calc_crc32 != LNT_NTOHL(crc32))  //检查报文CRC
		return AGENT_ERR_PKT_CRC;

	if(agent_ptr->const_exp_sz < agent_pkt_len) //判断数据长度是否小于协议结构要求长度
	{
		fprintf(stderr, "agent_ptr->const_exp_sz < agent_pkt_len\n");
		goto Done;
	}
	
	memset(&agent_pkt_head, 0, agent_pkt_len);
	memcpy(&agent_pkt_head, agent_ptr->exp_ptr, agent_pkt_len);

	if(LNT_NTOHS(agent_pkt_head.head) != AGENT_HEADER) //判断协议头
	{
		fprintf(stderr, "LNT_NTOHL(agent_pkt_head.head) != AGENT_HEADER\n");
		goto Done;
	}
	
	if(agent_pkt_head.type != AGENT_LNT)  //判断类型
	{
		fprintf(stderr, "agent_pkt_head.type != AGENT_LNT\n");
		goto Done;
	}
	
	memcpy(agent_ptr->data_ptr, &(agent_ptr->exp_ptr[12]), agent_ptr->data_sz);  //拷贝数据
	agent_ptr->data_sz = LNT_NTOHS(agent_pkt_head.length); //数据长度

	
	fprintf(stderr, "Agent Pkt Data Off Card Explain OK\n");

	if(LNT_NTOHS(agent_pkt_head.cmd) == AGENT_OFF_CARD_STAGE2) 
	{
		g_lnt_conf.pkt_sn_RO = LNT_NTOHL(agent_pkt_head.sn); //保存代理服务器返回的报文序列号

		fprintf(stderr, "-------------------PKT SN: %d\n", g_lnt_conf.pkt_sn_RO);
	}

	//阶段10 销卡结果上报应答
	//阶段24 充值结果上报应答
	if((LNT_NTOHS(agent_pkt_head.cmd) == AGENT_OFF_CARD_STAGE10)  || (LNT_NTOHS(agent_pkt_head.cmd) == AGENT_OFF_CARD_STAGE24)) 
		return AGENT_ERR_OK;

	return (unsigned char)agent_ptr->data_ptr[8];
	

Done:
	fprintf(stderr, "Agent Pkt Data Explain Off Card ERR UNKNOWN!\n");
	return AGENT_ERR_UNKNOWN;
}

/*
 * 充值数据解析
 */
static enum AGENT_ERR __agent_pkt_data_recharge_explain(struct agent_pkt_ptr *agent_ptr)
{
	if(agent_ptr == NULL)
		goto Done;

	unsigned int crc32 = 0;
	lnt_agent_packet_head_t agent_pkt_head;
	int agent_pkt_len = sizeof(lnt_agent_packet_head_t);
	unsigned int calc_crc32 = lnt_crc32((unsigned char *)agent_ptr->exp_ptr, agent_ptr->const_exp_sz - 4);
	memcpy(&crc32, &(agent_ptr->exp_ptr[agent_ptr->const_exp_sz - 4]), 4);

	if(calc_crc32 != LNT_NTOHL(crc32))  //检查报文CRC
		return AGENT_ERR_PKT_CRC;

	if(agent_ptr->const_exp_sz < agent_pkt_len) //判断数据长度是否小于协议结构要求长度
	{
		fprintf(stderr, "agent_ptr->const_exp_sz < agent_pkt_len\n");
		goto Done;
	}
	
	memset(&agent_pkt_head, 0, agent_pkt_len);
	memcpy(&agent_pkt_head, agent_ptr->exp_ptr, agent_pkt_len);

	if(LNT_NTOHS(agent_pkt_head.head) != AGENT_HEADER) //判断协议头
	{
		fprintf(stderr, "LNT_NTOHL(agent_pkt_head.head) != AGENT_HEADER\n");
		goto Done;
	}
	
	if(agent_pkt_head.type != AGENT_RECHARGE)  //判断类型
	{
		fprintf(stderr, "agent_pkt_head.type != AGENT_RECHARGE\n");
		goto Done;
	}
	
	memcpy(agent_ptr->data_ptr, &(agent_ptr->exp_ptr[12]), agent_ptr->data_sz);  //拷贝数据
	agent_ptr->data_sz = LNT_NTOHS(agent_pkt_head.length); //数据长度

	
	fprintf(stderr, "Agent Pkt Data Recharge Explain OK\n");

	if(LNT_NTOHS(agent_pkt_head.cmd) == AGENT_RECHARGE_STAGE2) 
	{
		g_lnt_conf.pkt_sn_RO = LNT_NTOHL(agent_pkt_head.sn); //保存代理服务器返回的报文序列号

		fprintf(stderr, "-------------------PKT SN: %d\n", g_lnt_conf.pkt_sn_RO);
	}


	//阶段24 充值结果上报应答
	if(LNT_NTOHS(agent_pkt_head.cmd) == AGENT_RECHARGE_STAGE18) 
		return AGENT_ERR_OK;

	return (unsigned char)agent_ptr->data_ptr[8];
	

Done:
	fprintf(stderr, "Agent Pkt Data Explain Recharge ERR UNKNOWN!\n");
	return AGENT_ERR_UNKNOWN;
}


#if 0
static void __agent_pkt_printf(enum AGENT_WAY way)
{
	fprintf(stderr, "way=%d\n", way);
	fprintf(stderr, "g_agent_now_len[%d]=%d\n", way, g_agent_now_len[way]);
	fprintf(stderr, "g_agent_next_idx[%d]=%d\n", way, g_agent_next_idx[way]);

	lib_printf_v2("way:", &(g_agent_data[way][0]), g_agent_now_len[way], 16);
}
#endif

static int __str_to_hex(unsigned char *str_data, unsigned char *hex_data, int str_len)
{
    int i, j, temp_len;
    int tempData;
    unsigned char *o_buf = str_data;

    for(i = 0; i < str_len; i++)
    {
        if((o_buf[i] >= '0') && (o_buf[i] <= '9'))         
        {
          	tempData = o_buf[i] - '0';
        }
        else if((o_buf[i] >= 'A') && (o_buf[i] <= 'F'))    
        {
          	tempData = o_buf[i] - 0x37;
        }
        else if((o_buf[i] >= 'a') && (o_buf[i] <= 'f'))   
        {
          	tempData = o_buf[i] - 0x57;
        }
        else
        {
          	return 0;  //illegal data
        }
        o_buf[i] = tempData;
    }
    
    for(temp_len = 0,j = 0; j < i; j += 2)
    {
        hex_data[temp_len++] = (o_buf[j] << 4) | o_buf[j+1];
    }
	
    return temp_len;
}

int lib_lnt_str_to_hex(unsigned char *str_data, unsigned char *hex_data, int str_len)
{
	return __str_to_hex(str_data, hex_data, str_len);
}

#if 0
static int __apdu_process(adpu_str_t *str, unsigned char *adpu, unsigned char *stat)
{
	if((str == NULL) || (adpu == NULL))
		return LNT_ERROR;

	unsigned char hex[128] = {0};
	char *buf = NULL, *delim = "[,\"";
	int len = 0, ret = 0;
	lnt_r_apdu_req_t apdu;
	lnt_r_apdu_ack_t ack;
	lnt_reader_ack_t rack;
	
	memset(&apdu,0,sizeof(apdu));
	memset(&rack,0,sizeof(rack));
	memset(&ack,0,sizeof(ack));

	buf = strtok((char *)adpu, delim);
	if(buf == NULL)
		return LNT_ERROR;

	ret = __str_to_hex((unsigned char *)buf, hex, strlen(buf));
	if(ret <= 0)
	{
		fprintf(stderr, "STR TO HEX [0] Failed!\n");
		return LNT_ERROR;
	}
	
	memcpy(&apdu, &hex[1], hex[1] + 1);//包含指令长度
	ret = dev_apdu_to_reader(&apdu, &rack, 200);  //[0]
	if(ret < 0)
	{
		if(stat != NULL)
			*stat = rack.stat;
		
		fprintf(stderr, "Adpu To Reader [0] Failed!\n");
		return LNT_ERROR;
	}

	ack.snum = 0x01;
	ack.len = rack.len - 4;
	memcpy(&ack.data, &rack.data[1], ack.len);
	str->s_str[len++] = 0x5b;//[
	str->s_str[len++] = 0x22;//"
	lib_hex_to_str((unsigned char *)&ack, ack.len + 2, &(str->s_str[len]));	
	len += 2 * (ack.len + 2);//一个字符占两个字节
	str->s_str[len++] = 0x22;//"
	str->s_str[len++] = 0x2c;//,
	
	buf = strtok(NULL, delim);
	if(buf == NULL)
		return LNT_ERROR;
	
	__str_to_hex((unsigned char *)buf, hex, strlen(buf));
	if(ret <= 0)
	{
		fprintf(stderr, "STR TO HEX [1] Failed!\n");
		return LNT_ERROR;
	}
		
	memcpy(&apdu, &hex[1], hex[1] + 1);
	ret = dev_apdu_to_reader(&apdu, &rack, 200);  //[1]
	if(ret < 0)
	{
		if(stat != NULL)
			*stat = rack.stat;
		
		fprintf(stderr, "Adpu To Reader [1] Failed!\n");
		return LNT_ERROR;
	}

	ack.snum = 0x02;
	ack.len = rack.len - 4;
	memcpy(&ack.data, &rack.data[1], ack.len);
	str->s_str[len++] = 0x22;//"
	lib_hex_to_str((unsigned char *)&ack, ack.len + 2, &(str->s_str[len]));
	if(ret <= 0)
	{
		fprintf(stderr, "STR TO HEX [2] Failed!\n");
		return LNT_ERROR;
	}
		
	len += 2 * (ack.len + 2);
	str->s_str[len++] = 0x22;//"
	str->s_str[len++] = 0x2c;//,
	
	buf = strtok(NULL, delim);
	if(buf == NULL)
		return LNT_ERROR;
		
	__str_to_hex((unsigned char *)buf, hex, strlen(buf));
	memcpy(&apdu, &hex[1], hex[1] + 1);
	ret = dev_apdu_to_reader(&apdu, &rack, 200); //[2]
	if(ret < 0)
	{
		if(stat != NULL)
			*stat = rack.stat;
		
		fprintf(stderr, "Adpu To Reader [2] Failed!\n");
		return LNT_ERROR;
	}

	ack.snum = 0x03;
	ack.len = rack.len - 4;
	memcpy(&ack.data, &rack.data[1], ack.len);
	str->s_str[len++] = 0x22;//"
	lib_hex_to_str((unsigned char *)&ack, ack.len + 2, &(str->s_str[len]));
	len += 2*(ack.len + 2);
	str->s_str[len++] = 0x22;//"
	str->s_str[len++] = 0x2c;//,
	
	buf = strtok(NULL,delim);
	if(buf == NULL)
		return LNT_ERROR;
		
	__str_to_hex((unsigned char *)buf, hex, strlen(buf));
	if(ret <= 0)
	{
		fprintf(stderr, "STR TO HEX [3] Failed!\n");
		return LNT_ERROR;
	}
		
	memcpy(&apdu,&hex[1], hex[1] + 1);
	ret = dev_apdu_to_reader(&apdu, &rack, 200); //[3]
	if(ret < 0)
	{
		if(stat != NULL)
			*stat = rack.stat;
		
		fprintf(stderr, "Adpu To Reader [3] Failed!\n");
		return LNT_ERROR;
	}

	ack.snum = 0x04;
	ack.len = rack.len - 4;
	memcpy(&ack.data, &rack.data[1], ack.len);
	str->s_str[len++] = 0x22;//"
	lib_hex_to_str((unsigned char *)&ack, ack.len + 2, &(str->s_str[len]));
	len += 2 * (ack.len + 2);
	str->s_str[len++] = 0x22;//"
	str->s_str[len++] = 0x2c;//,
	
	buf = strtok(NULL,delim);
	if(buf == NULL)
		return LNT_ERROR;
		
	__str_to_hex((unsigned char *)buf, hex, strlen(buf));
	if(ret <= 0)
	{
		fprintf(stderr, "STR TO HEX [4] Failed!\n");
		return LNT_ERROR;
	}
		
	memcpy(&apdu, &hex[1], hex[1] + 1);
	ret = dev_apdu_to_reader(&apdu, &rack, 200); //[4]
	if(ret < 0)
	{
		if(stat != NULL)
			*stat = rack.stat;
		
		fprintf(stderr, "Adpu To Reader [4] Failed!\n");
		return LNT_ERROR;
	}

	ack.snum = 0x05; //[5]
	ack.len = rack.len - 4;
	memcpy(&ack.data, &rack.data[1], ack.len);
	str->s_str[len++] = 0x22;//"
	lib_hex_to_str((unsigned char *)&ack, ack.len + 2, &(str->s_str[len]));
	len += 2 * (ack.len + 2);
	str->s_str[len++] = 0x22;//"
	str->s_str[len++] = 0x5d;//]
	str->s_len = strlen((char *)str->s_str);

	if(stat != NULL)
		*stat = LNT_MOC_ERROR_NORMAL;  //正常
	
	return LNT_EOK;
}
#endif


static int __apdu_process(adpu_str_t *a_str, unsigned char *s_adpu, unsigned char *stat)
{
	if((a_str == NULL) || (s_adpu == NULL))
		return LNT_ERROR;

	unsigned char hex[128] = {0};
	char *buf = NULL, *delim = "[,\"";
	int len = 0, ret = 0, err = -1;
	unsigned char num = 0;//指令个数
	lnt_r_apdu_req_t apdu;
	lnt_r_apdu_ack_t ack;
	lnt_reader_ack_t rack;
	unsigned short status = 0, exp_stat = 0;
			
	memset(&apdu,0,sizeof(apdu));
	memset(&rack,0,sizeof(rack));
	memset(&ack,0,sizeof(ack));
	
	while(1)
	{
		if(num == 0)
			buf = strtok((char *)s_adpu, delim);//分割出第一条指令
		else
			buf = strtok(NULL, delim);//分割出后续指令

		if(NULL != buf)
		{
			if(*buf == 0x5d)//"]",最后一条指令分割完成
				break;
			
			num++;//指令个数

			err = __str_to_hex((unsigned char *)buf, hex, strlen(buf));
			if(err <= 0)
			{
				fprintf(stderr, "str_to_hex failed!\n");
				return LNT_ERROR;
			}
	
			memcpy(&apdu,&hex[1], hex[1] + 1);//包含指令长度
			ret = dev_apdu_to_reader(&apdu, &rack, 100);
			if(ret <= 0)
			{
				fprintf(stderr, "dev_apdu_to_reader failed!\n");
				return LNT_ERROR;
			}

			ack.snum = num;
			ack.len = rack.len - 4;
			memcpy(&ack.data, &rack.data[1], ack.len);
			memcpy(&status, &rack.data[ack.len - 1], 2);
			memcpy(&exp_stat, &hex[err - 4], 2);
			
			if(status != exp_stat)
			{
				fprintf(stderr, "APDU retcode error!\n");
				return LNT_ERROR;
			}
			
			if(num == 1)//第一条指令前要带"["
				a_str->s_str[len++] = 0x5b;//[

			a_str->s_str[len++] = 0x22;//"
			lib_hex_to_str((unsigned char *)&ack, ack.len + 2, &(a_str->s_str[len]));
			len += 2*(ack.len + 2);//一个字符占两个字节
			a_str->s_str[len++] = 0x22;//"
			a_str->s_str[len++] = 0x2c;//,
		}
	}

	a_str->s_str[len - 1] = 0x5d;//]//最后一条指令后要带"]",-1是去掉最后的","
	a_str->s_len = strlen((char *)(a_str->s_str));//读卡器执行APDU指令后返回的字符串长度
	
	return LNT_EOK;
}













static void __lnt_register_stat_set(const unsigned int stat)
{
	lib_gcc_atmoic_set(&g_lnt_register_stat, stat);
}

static unsigned int __lnt_register_stat_get(void)
{
	return lib_gcc_atmoic_get(&g_lnt_register_stat);
}

#if 0
static void __lnt_register_sockfd_set(const unsigned int sockfd)
{
	lib_gcc_atmoic_set(&g_lnt_register_sockfd, sockfd);
}

static unsigned int __lnt_register_sockfd_get(void)
{
	return lib_gcc_atmoic_get(&g_lnt_register_sockfd);
}
#endif

static void __lnt_register_proc_code_set(const struct lnt_register_proc_code *proc)
{
	#if 0
	unsigned int iproc = 0;
	memcpy(&iproc, proc, 4);
	lib_gcc_atmoic_set(&g_lnt_register_proc_code, iproc);
	#endif
	if(proc == NULL)
		return;
	
	pthread_mutex_lock(g_register_proc_mutex);
	memcpy(&g_lnt_register_proc_code, proc, sizeof(struct lnt_register_proc_code));
	pthread_mutex_unlock(g_register_proc_mutex);
}

static struct lnt_register_proc_code __lnt_register_proc_code_get(void)
{
	#if 0
	unsigned int iproc = 0;
	struct lnt_register_proc_code proc;
	memset(&proc, 0, sizeof(struct lnt_register_proc_code));
	
	iproc = lib_gcc_atmoic_get(&g_lnt_register_proc_code);
	memcpy(&proc, &iproc, 4);
	
	return proc;
	#endif

	struct lnt_register_proc_code proc;
	memset(&proc, 0, sizeof(struct lnt_register_proc_code));

	pthread_mutex_lock(g_register_proc_mutex);
	memcpy(&proc, &g_lnt_register_proc_code, sizeof(struct lnt_register_proc_code));
	pthread_mutex_unlock(g_register_proc_mutex);

	return proc;
}








#if 0
static int __lnt_register_notify_put(const unsigned int notify)
{
	pthread_mutex_lock(&g_lnt_register_notify.mutex);

	g_lnt_register_notify.notify = notify;

	pthread_mutex_unlock(&g_lnt_register_notify.mutex);

	return pthread_cond_signal(&g_lnt_register_notify.cond);
}
#endif

static int __lnt_register_notify_put(const unsigned int notify)
{
	pthread_mutex_lock(g_lnt_register_notify_ptr->mutex);

	g_lnt_register_notify_ptr->notify = notify;

	pthread_mutex_unlock(g_lnt_register_notify_ptr->mutex);

	return pthread_cond_signal(g_lnt_register_notify_ptr->cond);
}

static int __lnt_register_notify_wait(unsigned int *notify)
{
	int err = -1;

	pthread_mutex_lock(g_lnt_register_notify_ptr->mutex);

	err = pthread_cond_wait(g_lnt_register_notify_ptr->cond, g_lnt_register_notify_ptr->mutex);
	*notify = g_lnt_register_notify_ptr->notify;
	
	pthread_mutex_unlock(g_lnt_register_notify_ptr->mutex);

	return err;	
}

#if 0
static int __lnt_register_notify_timedwait(const unsigned int msec, unsigned int *notify)
{
	int err = LNT_ERROR;
	struct timespec tspec;
	
	memset(&tspec, 0, sizeof(struct timespec));
	tspec.tv_sec = time(NULL) + msec / 1000;
	tspec.tv_nsec = msec * 1000 % 1000000;

	pthread_mutex_lock(&g_lnt_register_notify.mutex);

	err = pthread_cond_timedwait(&g_lnt_register_notify.cond, &g_lnt_register_notify.mutex, &tspec);
	if(err == ETIMEDOUT)
		err = LNT_ETIMEOUT;

	*notify = g_lnt_register_notify.notify;
		
	pthread_mutex_unlock(&g_lnt_register_notify.mutex);

	return err;
}
#endif


#if 0
static int __lnt_register_notify_wait(unsigned int *notify)
{
	int err = -1;

	pthread_mutex_lock(&g_lnt_register_notify.mutex);

	err = pthread_cond_wait(&g_lnt_register_notify.cond, &g_lnt_register_notify.mutex);
	*notify = g_lnt_register_notify.notify;
	
	pthread_mutex_unlock(&g_lnt_register_notify.mutex);

	return err;
}
#endif







static int __lnt_register_connect_to_server(const unsigned char *ipaddr, const unsigned int port)
{
	if(ipaddr == NULL)
		return LNT_ERROR;

	int sockfd = -1;
	struct lnt_register_proc_code proc;
	memset(&proc, 0, sizeof(struct lnt_register_proc_code));

	sockfd = lib_tcp_client_nonb_new((char *)ipaddr, port, 5);
	if(sockfd < 0)
	{
		fprintf(stderr, "Register Process: [connect LNT server] CONNECT failed, ip:%s, Port:%d\n", ipaddr, port);

		proc.proc = LNT_REGISTER_PROC_CONN;
		proc.fresult = LNT_FRESULT_E_NET; //网络出错
		__lnt_register_proc_code_set(&proc);
		
		return LNT_ERROR;
	}	

	proc.proc = LNT_REGISTER_PROC_CONN;
	proc.fresult = LNT_FRESULT_E_OK;
	__lnt_register_proc_code_set(&proc);
	
	//fprintf(stderr, "Register Process: [connect LNT server] success, ip:%s, Port:%d\n", ipaddr, port);
	
	return sockfd;
}

static void __lnt_register_disconnect_to_server(const int sockfd)
{
	if(sockfd > 0)
		lib_close(sockfd);
}





enum LNT_REGISTER_STAT lib_lnt_register_stat_get(void)
{
	return __lnt_register_stat_get();
}

lib_lnt_register_proc_code_t lib_lnt_register_proc_code_get(void)
{
	return __lnt_register_proc_code_get();
}

void lib_lnt_register_notify_put(const unsigned int notify)
{
	__lnt_register_notify_put(notify);
}




/*
 * 岭南通签到线程
 */
void *__lnt_register_entry(void *arg)
{

	lib_lnt_config_t *config = (lib_lnt_config_t *)arg;

	if(config == NULL)
		return (void *)LNT_NULL;

	memset(&g_lnt_conf, 0, sizeof(lib_lnt_config_t));
	memcpy(&g_lnt_conf, config, sizeof(lib_lnt_config_t));
	
	fprintf(stderr, "tty:%s\n", config->tty);
	fprintf(stderr, "baudrate:%d\n", config->baudrate);
	fprintf(stderr, "ipaddr:%s\n", config->ipaddr);
	fprintf(stderr, "port:%d\n", config->port);
	lib_printf("PKI", config->pki, 4);
	lib_printf("macaddr", config->macaddr, 6);

#if USE_NEW_USER_ID //add by zjc 2016-03-17
	memcpy(&config->userid[16 - UID_LEN],USER_ID,UID_LEN);
	printf("config->userid:%s\n",&config->userid[16 - UID_LEN]);
	lib_printf("config->userid", (unsigned char *)&config->userid, 16);
#endif

	int sockfd = -1;
	int ret = LNT_ERROR;
	unsigned char register_pid[8] = {0};
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	unsigned int notify = LNT_REGISTER_STAT_INIT;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	int txlen = 0;
	int agent_txlen = 0;
	unsigned char txbuf[512] = {0};
	unsigned char rxbuf[512] = {0};
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned int crc32 = 0;
	struct lnt_register_proc_code proc;
	lnt_packet_head_t pkt_head;
	struct agent_pkt_ptr *agent_ptr = (struct agent_pkt_ptr *)malloc(sizeof(struct agent_pkt_ptr));
	memset(agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	int con_faild_cnt = 0;

	#if CONFS_USING_READER_UPDATE
	enum UPDATE_STATUS up_stat = READER_UPDATE_FAILED; //读卡器升级状态
	#endif
	
	__lnt_register_stat_set(LNT_REGISTER_STAT_INIT);  
	
	while(1)
	{	
		#if CONFS_USING_READER_UPDATE
		up_stat = lib_get_update_status();
		//printf("---------in linnantong, up_stat:%d\n", up_stat);
		while(up_stat == 1) //读卡器升级期间不能签到,以免干扰升级!!!
		{
			//printf("---------------sleep in linnantong ...\n");
			lib_sleep(LNT_REG_CTE_SLEEP);
		}
		#endif
		
		fprintf(stderr, "LNT beigin register process\n");

		printf("UID_LEN:%d,USER_ID:%s\n",UID_LEN,USER_ID);
		
		__lnt_register_stat_set(LNT_REGISTER_STAT_ING); //正在签到过程
		
		/* 第一步骤:查询票卡信息 */
		lnt_qry_ticket_info_ack_t qry_ticket_ack;
		memset(&proc, 0, sizeof(struct lnt_register_proc_code));
		memset(&qry_ticket_ack, 0, sizeof(lnt_qry_ticket_info_ack_t));
		ret = dev_qry_ticket_info(&qry_ticket_ack, &stat, 500);
		if(ret < 0)
		{
			fprintf(stderr, "Register Process: [query ticket] UART failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_QUERTICKET;
			proc.fresult = LNT_FRESULT_E_UART;  //串口出错
			proc.stat = stat;
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		fprintf(stderr, "PMFLAG:0x%02x\n", qry_ticket_ack.ticket.pmflag);

		proc.proc = LNT_REGISTER_PROC_QUERTICKET;
		proc.fresult = LNT_FRESULT_E_OK;
		proc.stat = stat;
		time(&(proc.time));
		__lnt_register_proc_code_set(&proc); 

		fprintf(stderr, "Register Process: [query ticket] success.\n\n");
	
		/* 第二步骤:DD01-DD02组包 */
		lnt_packet_register_stage1_CTS_t stage1;
		memset(&pkt_head, 0, pkt_head_len);
		memset(&stage1, 0,sizeof(lnt_packet_register_stage1_CTS_t));
		memset(&proc, 0, sizeof(struct lnt_register_proc_code));
		
		memcpy(stage1.userid, config->userid, 16);  //用户ID
		stage1.appstep = 0x00;  //应用步骤
		memset(&stage1.pki, 0xff, 4); //PKI

		pkt_head.head = LNT_HTONS(LNT_REGISTER_STAGE1);
		pkt_head.version = 0x01;
		pkt_head.length = LNT_HTONS(sizeof(lnt_packet_register_stage1_CTS_t) - 4);

		stage1.ilen = 0;
		stage1.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage1, sizeof(lnt_packet_register_stage1_CTS_t) - 4));
	
		txlen = pkt_head_len + sizeof(lnt_packet_register_stage1_CTS_t);
		memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
		memcpy(&(txbuf[pkt_head_len]), &stage1, sizeof(lnt_packet_register_stage1_CTS_t)); //拷贝报文体


		/* 代理服务器组包 DD01 */
		__agent_pkt_hd_set(AGENT_WAY_REG, AGENT_REGISTER_STAGE1, AGENT_ATTR_PASS);
		__agent_pkt_set_sn(AGENT_WAY_REG, 2015); //报文序列号
		__agent_pkt_data_set(AGENT_WAY_REG, &register_pid, 8);  //PID
		__agent_pkt_data_set(AGENT_WAY_REG, config->macaddr, 6);  //MAC
		__agent_pkt_data_set(AGENT_WAY_REG, config->pki, 4);  //pki
		__agent_pkt_data_set(AGENT_WAY_REG, &txlen, 1);  //数据长度
		__agent_pkt_data_set(AGENT_WAY_REG, txbuf, txlen);  //数据
		__agent_pkt_crc32_set(AGENT_WAY_REG);
		agent_txlen = __agent_pkt_data_get(AGENT_WAY_REG, agent_txbuf);
		
#ifdef LNT_REG_PROC_DEBUG
		lib_printf_v2("--------------AGENT REGISTER PROCESS DD01---------------", agent_txbuf, agent_txlen , 16);
		fprintf(stderr, "--------------------------END---------------------\n");
#endif

		//__lnt_register_disconnect_to_server(sockfd);  //可能是界面显示错位或乱码的原因所在! by zjc at 2016-10-12
		sockfd = -1;  
		sockfd = __lnt_register_connect_to_server((unsigned char *)config->ipaddr, config->port);  //连接到代理网关
		if(sockfd == LNT_ERROR)
		{
			if(con_faild_cnt > 65535)
				con_faild_cnt = 0;
			
			con_faild_cnt++;
			printf("-----------con_faild_cnt:%d\n", con_faild_cnt);
			
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 

			/* 修改重连时间间隔 add by zjc at 2016-12-23 */
			if(con_faild_cnt <= 5)
				lib_sleep(LNT_REG_CTE_SLEEP); //前5次失败休眠15秒重连
			else
				lib_sleep(2*4*LNT_REG_CTE_SLEEP); //失败超过5次则休眠2分钟重连

			continue;
		}
		else
			con_faild_cnt = 0; //连接成功后连接失败计数清零
				
		lib_setfd_noblock(sockfd); //设置为非阻塞

		ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
		if(ret <= 0)
		{
			fprintf(stderr, "Register Process: [DD01] WRITE failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_DD01;
			proc.fresult = LNT_FRESULT_E_NET; //网络出错
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 	
			
			__lnt_register_disconnect_to_server(sockfd);
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		proc.proc = LNT_REGISTER_PROC_DD01;
		proc.fresult = LNT_FRESULT_E_OK; 
		time(&(proc.time));
		__lnt_register_proc_code_set(&proc); 	

		fprintf(stderr, "Register Process: [DD01] WRITE success\n\n");
		
#ifdef LNT_USING_SELECT_RD		
		ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
#else
		ret = __lnt_net_read(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
#endif
		if(ret <= 0)
		{
			fprintf(stderr, "Register Process: [DD02] READ failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_DD02;
			proc.fresult = LNT_FRESULT_E_NET; //网络出错
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 	
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

#ifdef LNT_REG_PROC_DEBUG
		lib_printf_v2("--------------AGENT REGISTER PROCESS DD02---------------", agent_rxbuf, ret, 16);
		fprintf(stderr, "--------------------------END---------------------\n\n");
#endif

		/* 代理服务器解包 DD02 */
		memset(&rxbuf, 0, sizeof(rxbuf));
		agent_ptr->exp_ptr = (char *)&agent_rxbuf;
		agent_ptr->const_exp_sz = ret;
		agent_ptr->extra_ptr = NULL;
		agent_ptr->extra_sz = 0;
		agent_ptr->data_ptr = (char *)&rxbuf;
		agent_ptr->data_sz = sizeof(lnt_packet_register_stage2_STC_t) + pkt_head_len;
		agent_err = __agent_pkt_data_register_explain(agent_ptr);
		if(agent_err != AGENT_ERR_OK)
		{
			proc = __lnt_register_proc_code_get();
			proc.proc = LNT_REGISTER_PROC_DD02;
			proc.agent_err = agent_err;  //设置代理错误码
			__lnt_register_proc_code_set(&proc);
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		lnt_packet_register_stage2_STC_t stage2;
		memset(&stage2, 0,sizeof(lnt_packet_register_stage2_STC_t));
		memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
		memcpy(&pkt_head, rxbuf, pkt_head_len);
   		memcpy(&stage2, &rxbuf[pkt_head_len], sizeof(lnt_packet_register_stage2_STC_t));

		if(stage2.retcode != 0x00) //返回码
		{
			fprintf(stderr, "Register Process: [DD02] RETURN CODE failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD02;
			proc.fresult = LNT_FRESULT_E_RETCODE;
			proc.stat = stage2.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}
		
		crc32 = lnt_crc32((unsigned char *)&stage2, sizeof(lnt_packet_register_stage2_STC_t) - 4); //计算校验
		if(LNT_NTOHL(stage2.crc32) != crc32)  //检验码
		{
			fprintf(stderr, "Register Process: [DD02] CRC failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD02;
			proc.fresult = LNT_FRESULT_E_CRC;
			proc.stat = stage2.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		if(stage2.indcode != 0x00) //流程指示码  0x00:继续流程, 0xFF:结束流程
		{
			fprintf(stderr, "Register Process: [DD02] STEP failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD02;
			proc.fresult = LNT_FRESULT_E_STEP; //步骤出错
			proc.stat = stage2.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		fprintf(stderr, "Register Process: [DD02] READ success\n\n");

		proc.proc = LNT_REGISTER_PROC_DD02;
		proc.fresult = LNT_FRESULT_E_OK; 
		proc.stat = stage2.retcode; 
		time(&(proc.time));
		__lnt_register_proc_code_set(&proc); 
		
		/* 第三步骤:DD03-DD04组包 */
		lnt_r_init_ack_t r_init_ack;
		memset(&r_init_ack, 0, sizeof(lnt_r_init_ack_t));
	
		ret = dev_CLA_INS_pkg(stage2.INFO, stage2.ilen, &r_init_ack, sizeof(lnt_r_init_ack_t), &stat, 400);
		if(ret < 0)
		{
			fprintf(stderr, "Register Process: [R INIT] UART failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_R_INIT;
			proc.fresult = LNT_FRESULT_E_UART;  //串口出错
			proc.stat = stat;
			proc.sw = r_init_ack.sw;  //响应状态码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;			
		}

		memcpy(&(g_lnt_conf.pki_r), &(r_init_ack.pki), 4);  //PKI

		if((r_init_ack.sw != LNT_SW_9000) || (stat != LNT_MOC_ERROR_NORMAL))
		{
			fprintf(stderr, "Register Process: [R INIT] STAT failed!\n\n");
	
			proc.proc = LNT_REGISTER_PROC_R_INIT;
			proc.fresult = LNT_FRESULT_E_STAT;  //状态码出错
			proc.stat = stat;
			proc.sw = r_init_ack.sw;  //响应状态码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;	
		}
		
		lnt_packet_register_stage3_CTS_t stage3;
		memset(&txbuf, 0, sizeof(txbuf));
		memset(&rxbuf, 0, sizeof(rxbuf));
		memset(&pkt_head, 0, pkt_head_len);
		memset(&stage3, 0,sizeof(lnt_packet_register_stage3_CTS_t));
		memset(&proc, 0, sizeof(struct lnt_register_proc_code));
		
		memcpy(stage3.userid, config->userid, 16);  //用户ID
		stage3.appstep = 0x01;  //应用步骤
		memcpy(&stage3.pki, &r_init_ack.pki, 4);  //PKI
		memcpy(&config->pki, &r_init_ack.pki, 4);  //保存PKI
		memcpy(&config->pki_RO, &r_init_ack.pki, 4);  //保存PKI
		memcpy(&stage3.INFO, &r_init_ack, sizeof(lnt_r_init_ack_t));
		
		pkt_head.head = LNT_HTONS(LNT_REGISTER_STAGE3);
    		pkt_head.version = 0x01;
    		pkt_head.length = LNT_HTONS(sizeof(lnt_packet_register_stage3_CTS_t) - 4);

		stage3.ilen = sizeof(lnt_r_init_ack_t);
		stage3.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage3, sizeof(lnt_packet_register_stage3_CTS_t) - 4));
	
		txlen = pkt_head_len + sizeof(lnt_packet_register_stage3_CTS_t);
		memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
		memcpy(&(txbuf[pkt_head_len]), &stage3, sizeof(lnt_packet_register_stage3_CTS_t)); //拷贝报文体
		
		/* 代理服务器组包 DD03 */
		__agent_pkt_hd_set(AGENT_WAY_REG, AGENT_REGISTER_STAGE3, AGENT_ATTR_PASS);
		__agent_pkt_data_set(AGENT_WAY_REG, &register_pid, 8);  //PID
		__agent_pkt_data_set(AGENT_WAY_REG, config->macaddr, 6);  //MAC
		__agent_pkt_data_set(AGENT_WAY_REG, config->pki, 4);  //pki
		__agent_pkt_data_set(AGENT_WAY_REG, &txlen, 1);  //数据长度
		__agent_pkt_data_set(AGENT_WAY_REG, txbuf, txlen);  //数据
		__agent_pkt_crc32_set(AGENT_WAY_REG);
		agent_txlen = __agent_pkt_data_get(AGENT_WAY_REG, agent_txbuf);

#ifdef LNT_REG_PROC_DEBUG
		lib_printf_v2("--------------AGENT REGISTER PROCESS DD03---------------", agent_txbuf, agent_txlen, 16);
		fprintf(stderr, "--------------------------END---------------------\n");
#endif

		ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
		if(ret <= 0)
		{
			fprintf(stderr, "Register Process: [DD03] WRITE failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_DD03;
			proc.fresult = LNT_FRESULT_E_NET; //网络出错
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 	
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		proc.proc = LNT_REGISTER_PROC_DD03;
		proc.fresult = LNT_FRESULT_E_OK; 
		time(&(proc.time));
		__lnt_register_proc_code_set(&proc); 	

		fprintf(stderr, "Register Process: [DD03] WRITE success\n\n");
		
#ifdef LNT_USING_SELECT_RD		
		ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
#else
		ret = __lnt_net_read(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
#endif
		if(ret <= 0)
		{
			fprintf(stderr, "Register Process: [DD04] READ failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_DD04;
			proc.fresult = LNT_FRESULT_E_NET; //网络出错
			__lnt_register_proc_code_set(&proc); 	
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

#ifdef LNT_REG_PROC_DEBUG
		lib_printf_v2("--------------AGENT REGISTER PROCESS DD04---------------", agent_rxbuf, ret, 16);
		fprintf(stderr, "--------------------------END---------------------\n");
#endif


	/* 代理服务器解包 DD04 */
		memset(&rxbuf, 0, sizeof(rxbuf));
		agent_ptr->exp_ptr = (char *)&agent_rxbuf;
		agent_ptr->const_exp_sz = ret;
		agent_ptr->extra_ptr = NULL;
		agent_ptr->extra_sz = 0;
		agent_ptr->data_ptr = (char *)&rxbuf;
		agent_ptr->data_sz = sizeof(lnt_packet_register_stage4_STC_t) + pkt_head_len;
		agent_err = __agent_pkt_data_register_explain(agent_ptr);
		if(agent_err != AGENT_ERR_OK)
		{
			proc = __lnt_register_proc_code_get();
			proc.proc = LNT_REGISTER_PROC_DD04;
			proc.agent_err = agent_err;  //设置代理错误码
			__lnt_register_proc_code_set(&proc);
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		lnt_packet_register_stage4_STC_t stage4;
		memset(&stage4, 0,sizeof(lnt_packet_register_stage4_STC_t));
		memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
		memcpy(&pkt_head, rxbuf, pkt_head_len);
   		memcpy(&stage4, &rxbuf[pkt_head_len], sizeof(lnt_packet_register_stage4_STC_t));

		if(stage4.retcode != 0x00) //返回码
		{
			fprintf(stderr, "Register Process: [DD04] RETURN CODE failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD04;
			proc.fresult = LNT_FRESULT_E_RETCODE;
			proc.stat = stage4.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}
		
		crc32 = lnt_crc32((unsigned char *)&stage4, sizeof(lnt_packet_register_stage4_STC_t) - 4);
		if(LNT_NTOHL(stage4.crc32) != crc32)  //检验码
		{
			fprintf(stderr, "Register Process: [DD04] CRC failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD04;
			proc.fresult = LNT_FRESULT_E_CRC;
			proc.stat = stage4.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		if(stage4.indcode != 0x00) //流程指示码  ox00:继续流程, 0xFF:结束流程
		{
			fprintf(stderr, "Register Process: [DD04] STEP failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD04;
			proc.fresult = LNT_FRESULT_E_STEP; //步骤出错
			proc.stat = stage2.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		fprintf(stderr, "Register Process: [DD04] READ success\n\n");

		proc.proc = LNT_REGISTER_PROC_DD04;
		proc.fresult = LNT_FRESULT_E_OK; 
		proc.stat = stage4.retcode; 
		time(&(proc.time));
		__lnt_register_proc_code_set(&proc); 


		/* 第四步骤:DD05-DD06组包 */
		lnt_r_ac_login_1_ack_t ac_login1_ack;
		memset(&ac_login1_ack, 0, sizeof(lnt_r_ac_login_1_ack_t));
	
		ret = dev_CLA_INS_pkg(stage4.INFO, stage4.ilen, &ac_login1_ack, sizeof(lnt_r_ac_login_1_ack_t), &stat, 400);
		if(ret < 0)
		{
			fprintf(stderr, "Register Process: [AC LOGIN1] UART failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_R_AC_LOGIN_1;
			proc.fresult = LNT_FRESULT_E_UART;  //串口出错
			proc.stat = stat;
			proc.sw = ac_login1_ack.sw;  //响应状态码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;			
		}

		if((ac_login1_ack.sw != LNT_SW_9000) || (stat != LNT_MOC_ERROR_NORMAL))
		{
			fprintf(stderr, "Register Process: [AC LOGIN1] STAT failed!\n\n");
	
			proc.proc = LNT_REGISTER_PROC_R_AC_LOGIN_1;
			proc.fresult = LNT_FRESULT_E_STAT;  //状态码出错
			proc.stat = stat;
			proc.sw = ac_login1_ack.sw;  //响应状态码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;	
		}

		lnt_packet_register_stage5_CTS_t stage5;
		memset(&txbuf, 0, sizeof(txbuf));
		memset(&rxbuf, 0, sizeof(rxbuf));
		
		memset(&pkt_head, 0, pkt_head_len);
		memset(&stage5, 0,sizeof(lnt_packet_register_stage5_CTS_t));
		memset(&proc, 0, sizeof(struct lnt_register_proc_code));
		
		memcpy(stage5.userid, config->userid, 16);  //用户ID
		stage5.appstep = 0x01;  //应用步骤
		memcpy(&stage5.pki, &r_init_ack.pki, 4);  //PKI
		memcpy(&stage5.INFO, &ac_login1_ack, sizeof(lnt_r_ac_login_1_ack_t));
		
		pkt_head.head = LNT_HTONS(LNT_REGISTER_STAGE5);
    		pkt_head.version = 0x01;
    		pkt_head.length = LNT_HTONS(sizeof(lnt_packet_register_stage5_CTS_t) - 4);

		stage5.ilen = sizeof(lnt_r_ac_login_1_ack_t);
		stage5.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage5, sizeof(lnt_packet_register_stage5_CTS_t) - 4));
	
		txlen = pkt_head_len + sizeof(lnt_packet_register_stage5_CTS_t);
		memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
		memcpy(&(txbuf[pkt_head_len]), &stage5, sizeof(lnt_packet_register_stage5_CTS_t)); //拷贝报文体

		/* 代理服务器组包 DD05 */
		__agent_pkt_hd_set(AGENT_WAY_REG, AGENT_REGISTER_STAGE5, AGENT_ATTR_PASS);
		__agent_pkt_data_set(AGENT_WAY_REG, &register_pid, 8);  //PID
		__agent_pkt_data_set(AGENT_WAY_REG, config->macaddr, 6);  //MAC
		__agent_pkt_data_set(AGENT_WAY_REG, config->pki, 4);  //pki
		__agent_pkt_data_set(AGENT_WAY_REG, &txlen, 1);  //数据长度
		__agent_pkt_data_set(AGENT_WAY_REG, txbuf, txlen);  //数据
		__agent_pkt_crc32_set(AGENT_WAY_REG);
		agent_txlen = __agent_pkt_data_get(AGENT_WAY_REG, agent_txbuf);


#ifdef LNT_REG_PROC_DEBUG
		lib_printf_v2("--------------AGENT REGISTER PROCESS DD05---------------", agent_txbuf, agent_txlen, 16);
		fprintf(stderr, "--------------------------END---------------------\n");
#endif

		ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
		if(ret <= 0)
		{
			fprintf(stderr, "Register Process: [DD05] WRITE failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_DD05;
			proc.fresult = LNT_FRESULT_E_NET; //网络出错
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 	
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		proc.proc = LNT_REGISTER_PROC_DD05;
		proc.fresult = LNT_FRESULT_E_OK; 
		__lnt_register_proc_code_set(&proc); 	

		fprintf(stderr, "Register Process: [DD05] WRITE success\n\n");

#ifdef LNT_USING_SELECT_RD		
		ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
#else
		ret = __lnt_net_read(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
#endif
		if(ret <= 0)
		{
			fprintf(stderr, "Register Process: [DD06] READ failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_DD06;
			proc.fresult = LNT_FRESULT_E_NET; //网络出错
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 	
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

#ifdef LNT_REG_PROC_DEBUG
		lib_printf_v2("--------------AGENT REGISTER PROCESS DD06---------------", agent_rxbuf, ret, 16);
		fprintf(stderr, "--------------------------END---------------------\n");
#endif

	/* 代理服务器解包 DD06 */
		memset(&rxbuf, 0, sizeof(rxbuf));
		agent_ptr->exp_ptr = (char *)&agent_rxbuf;
		agent_ptr->const_exp_sz = ret;
		agent_ptr->extra_ptr = NULL;
		agent_ptr->extra_sz = 0;
		agent_ptr->data_ptr = (char *)&rxbuf;
		agent_ptr->data_sz = sizeof(lnt_packet_register_stage6_STC_t) + pkt_head_len;
		agent_err = __agent_pkt_data_register_explain(agent_ptr);
		if(agent_err != AGENT_ERR_OK)
		{
			proc = __lnt_register_proc_code_get();
			proc.proc = LNT_REGISTER_PROC_DD06;
			proc.agent_err = agent_err;  //设置代理错误码
			__lnt_register_proc_code_set(&proc);
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		lnt_packet_register_stage6_STC_t stage6;
		memset(&stage6, 0,sizeof(lnt_packet_register_stage6_STC_t));
		memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
		memcpy(&pkt_head, rxbuf, pkt_head_len);
   		memcpy(&stage6, &rxbuf[pkt_head_len], sizeof(lnt_packet_register_stage6_STC_t));

		printf("\nRegister Process: [DD06] RETURN CODE:%02X\n",stage6.retcode);

		if(stage6.retcode != 0x00) //返回码
		{
			fprintf(stderr, "Register Process: [DD06] RETURN CODE failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD06;
			proc.fresult = LNT_FRESULT_E_RETCODE;
			proc.stat = stage6.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}
		
		crc32 = lnt_crc32((unsigned char *)&stage6, sizeof(lnt_packet_register_stage6_STC_t) - 4);
		if(LNT_NTOHL(stage6.crc32) != crc32)  //检验码
		{
			fprintf(stderr, "Register Process: [DD06] CRC failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD06;
			proc.fresult = LNT_FRESULT_E_CRC;
			proc.stat = stage6.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		if(stage6.indcode != 0x00) //流程指示码  ox00:继续流程, 0xFF:结束流程
		{
			fprintf(stderr, "Register Process: [DD06] STEP failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD06;
			proc.fresult = LNT_FRESULT_E_STEP; //步骤出错
			proc.stat = stage6.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		fprintf(stderr, "Register Process: [DD06] READ success\n\n");

		proc.proc = LNT_REGISTER_PROC_DD06;
		proc.fresult = LNT_FRESULT_E_OK; 
		proc.stat = stage6.retcode; 
		time(&(proc.time));
		__lnt_register_proc_code_set(&proc); 
			
		/* 第五步骤:DD07-DD08组包 */
		lnt_r_ac_login_2_ack_t ac_login2_ack;
		memset(&ac_login2_ack, 0, sizeof(lnt_r_ac_login_2_ack_t));

		ret = dev_CLA_INS_pkg(stage6.INFO, stage6.ilen, &ac_login2_ack, sizeof(lnt_r_ac_login_2_ack_t), &stat, 500);
		if(ret < 0)
		{
			fprintf(stderr, "Register Process: [AC LOGIN2] UART failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_R_AC_LOGIN_2;
			proc.fresult = LNT_FRESULT_E_UART;  //串口出错
			proc.stat = stat;
			proc.sw = ac_login2_ack.sw;  //响应状态码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;			
		}

		if((ac_login2_ack.sw != LNT_SW_9000) || (stat != LNT_MOC_ERROR_NORMAL))
		{
			fprintf(stderr, "Register Process: [AC LOGIN2] STAT failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_R_AC_LOGIN_2;
			proc.fresult = LNT_FRESULT_E_STAT;  //状态码出错
			proc.stat = stat;
			proc.sw = ac_login2_ack.sw;  //响应状态码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;	
		}

		lnt_packet_register_stage7_CTS_t stage7;
		memset(&txbuf, 0, sizeof(txbuf));
		memset(&rxbuf, 0, sizeof(rxbuf));
		
		memset(&pkt_head, 0, pkt_head_len);
		memset(&stage7, 0,sizeof(lnt_packet_register_stage7_CTS_t));
		memset(&proc, 0, sizeof(struct lnt_register_proc_code));
		
		memcpy(stage7.userid, config->userid, 16);  //用户ID
		stage7.appstep = 0x01;  //应用步骤
		memcpy(&stage7.pki, &r_init_ack.pki, 4);  //PKI
		memcpy(&stage7.INFO, &ac_login2_ack, sizeof(lnt_r_ac_login_2_ack_t));
		
		pkt_head.head = LNT_HTONS(LNT_REGISTER_STAGE7);
    		pkt_head.version = 0x01;
    		pkt_head.length = LNT_HTONS(sizeof(lnt_packet_register_stage7_CTS_t) - 4);

		stage7.ilen = sizeof(lnt_r_ac_login_2_ack_t);
		stage7.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage7, sizeof(lnt_packet_register_stage7_CTS_t) - 4));
	
		txlen = pkt_head_len + sizeof(lnt_packet_register_stage7_CTS_t);
		memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
		memcpy(&(txbuf[pkt_head_len]), &stage7, sizeof(lnt_packet_register_stage7_CTS_t)); //拷贝报文体

		/* 代理服务器组包 DD07 */
		__agent_pkt_hd_set(AGENT_WAY_REG, AGENT_REGISTER_STAGE7, AGENT_ATTR_PASS);
		__agent_pkt_data_set(AGENT_WAY_REG, &register_pid, 8);  //PID
		__agent_pkt_data_set(AGENT_WAY_REG, config->macaddr, 6);  //MAC
		__agent_pkt_data_set(AGENT_WAY_REG, config->pki, 4);  //pki
		__agent_pkt_data_set(AGENT_WAY_REG, &txlen, 1);  //数据长度
		__agent_pkt_data_set(AGENT_WAY_REG, txbuf, txlen);  //数据
		__agent_pkt_crc32_set(AGENT_WAY_REG);
		agent_txlen = __agent_pkt_data_get(AGENT_WAY_REG, agent_txbuf);

#ifdef LNT_REG_PROC_DEBUG
		lib_printf_v2("--------------AGENT REGISTER PROCESS DD07---------------", txbuf, txlen, 16);
		fprintf(stderr, "--------------------------END---------------------\n");
#endif

		ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
		if(ret <= 0)
		{
			fprintf(stderr, "Register Process: [DD07] WRITE failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_DD07;
			proc.fresult = LNT_FRESULT_E_NET; //网络出错
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 	
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		proc.proc = LNT_REGISTER_PROC_DD07;
		proc.fresult = LNT_FRESULT_E_OK; 
		time(&(proc.time));
		__lnt_register_proc_code_set(&proc); 	

		fprintf(stderr, "Register Process: [DD07] WRITE success\n\n");
		
#ifdef LNT_USING_SELECT_RD		
		ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
#else
		ret = __lnt_net_read(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
#endif
		if(ret <= 0)
		{
			fprintf(stderr, "Register Process: [DD08] READ failed!\n\n");

			proc.proc = LNT_REGISTER_PROC_DD08;
			proc.fresult = LNT_FRESULT_E_NET; //网络出错
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 	
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

#ifdef LNT_REG_PROC_DEBUG
		lib_printf_v2("--------------AGENT REGISTER PROCESS DD08---------------", agent_txbuf, ret, 16);
		fprintf(stderr, "--------------------------END---------------------\n");
#endif

		/* 代理服务器解包 DD08 */
		memset(&rxbuf, 0, sizeof(rxbuf));
		agent_ptr->exp_ptr = (char *)&agent_rxbuf;
		agent_ptr->const_exp_sz = ret;
		agent_ptr->extra_ptr = NULL;
		agent_ptr->extra_sz = 0;
		agent_ptr->data_ptr = (char *)&rxbuf;
		agent_ptr->data_sz = sizeof(lnt_packet_register_stage8_STC_t) + pkt_head_len;
		agent_err = __agent_pkt_data_register_explain(agent_ptr);
		if(agent_err != AGENT_ERR_OK)
		{
			proc = __lnt_register_proc_code_get();
			proc.proc = LNT_REGISTER_PROC_DD08;
			proc.agent_err = agent_err;  //设置代理错误码
			__lnt_register_proc_code_set(&proc);
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}	

		lnt_packet_register_stage8_STC_t stage8;
		memset(&stage8, 0,sizeof(lnt_packet_register_stage8_STC_t));
		memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
		memcpy(&pkt_head, rxbuf, pkt_head_len);
   		memcpy(&stage8, &rxbuf[pkt_head_len], sizeof(lnt_packet_register_stage8_STC_t));
		crc32 = lnt_crc32((unsigned char *)&stage8, sizeof(lnt_packet_register_stage8_STC_t) - 4);

		if(stage8.retcode != 0x00) //返回码
		{
			fprintf(stderr, "Register Process: [DD08] RETURN CODE failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD08;
			proc.fresult = LNT_FRESULT_E_RETCODE;
			proc.stat = stage8.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}
		
		if(LNT_NTOHL(stage8.crc32) != crc32)  //检验码
		{
			fprintf(stderr, "Register Process: [DD08] CRC failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD08;
			proc.fresult = LNT_FRESULT_E_CRC;
			proc.stat = stage8.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}

		/* 这里是0xFF结束流程 */
		if(stage8.indcode == 0x00) //流程指示码  ox00:继续流程, 0xFF:结束流程
		{
			fprintf(stderr, "Register Process: [DD08] STEP failed!\n\n");
			
			proc.proc = LNT_REGISTER_PROC_DD08;
			proc.fresult = LNT_FRESULT_E_STEP; //步骤出错
			proc.stat = stage8.retcode; //报文返回码
			time(&(proc.time));
			__lnt_register_proc_code_set(&proc); 
			__lnt_register_stat_set(LNT_REGISTER_STAT_FAIL); 
			__lnt_register_disconnect_to_server(sockfd);
			
			lib_sleep(LNT_REG_CTE_SLEEP);
			continue;
		}
		
		fprintf(stderr, "Register Process: [DD08] READ success\n\n");

		proc.proc = LNT_REGISTER_PROC_DD08;
		proc.fresult = LNT_FRESULT_E_OK; 
		proc.stat = stage8.retcode; 
		time(&(proc.time));
		__lnt_register_proc_code_set(&proc);
		__lnt_register_stat_set(LNT_REGISTER_STAT_OK);  //签到成功
		__lnt_register_disconnect_to_server(sockfd); //关闭套接字
		
		memcpy(&g_lnt_conf.pki_RO, r_init_ack.pki, 4);  //签到成功,保存PKI

		fprintf(stderr, "Register Process: register process success, Close Sockfd %d\n\n", sockfd);

#ifdef LNT_EXE_DEBUG  //测试使用
		break;
#endif

		#if CONFS_USING_READER_UPDATE
		up_stat = lib_get_update_status();
		printf("in linnantong, reader update status:%d\n", up_stat);
		#endif
		
		__lnt_register_notify_wait(&notify); //签到成功后,线程休眠
		if((notify == LNT_REGISTER_STAT_RE) || (__lnt_register_stat_get() == LNT_REGISTER_STAT_INIT))  //重新签到
		{
		#if CONFS_USING_READER_UPDATE
			if(up_stat != READER_UPDATE_ING) //读卡器升级期间不能操作读卡器!!!
			{
				fprintf(stderr, "LNT RE-register process\n");
				continue;
			}
			else
			{
				fprintf(stderr, "Reader updating, LNT RE-register process exit ...\n");
				break;
			}
			
		#else
			fprintf(stderr, "LNT RE-register process\n");
			continue;
		#endif
		}
	}

	if(agent_ptr != NULL)
	{
		free(agent_ptr);
		agent_ptr = NULL;
	}

	//return NULL;
	return lib_thread_exit((void*)NULL);//
}

#if 0
/*
 * 会员押金处理
 */
int lib_lnt_member_deposit_process(enum LNT_MEMBER_OP op, unsigned char pid[8], lnt_member_proc_code_t *proc)
{
	if(proc == NULL)
		return LNT_ERROR;
	
	int ret = LNT_ERROR;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	int sockfd = -1;
	unsigned char rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	lnt_qry_ticket_info_ack_t qry_ticket_ack;
	lnt_r_deposit_init_req_t dpinit_req;
	lnt_r_deposit_init_ack_t dpinit_ack;
	lnt_r_deposit_process_req_t dproc_req;
	lnt_r_deposit_process_ack_t dproc_ack;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	unsigned int txlen = 0;
	unsigned int crc32 = 0;
	lnt_packet_head_t pkt_head;
	lnt_member_process_stage1_CTS_t stage1;
	lnt_member_process_stage2_STC_t stage2;

	memset(&qry_ticket_ack, 0, sizeof(lnt_qry_ticket_info_ack_t));
	memset(&dpinit_req, 0, sizeof(lnt_r_deposit_init_req_t));
	memset(&dpinit_ack, 0, sizeof(lnt_r_deposit_init_ack_t));
	memset(&dproc_req, 0, sizeof(lnt_r_deposit_process_req_t));
	memset(&dproc_ack, 0, sizeof(lnt_r_deposit_process_ack_t));
	memset(&pkt_head, 0, pkt_head_len);
	memset(&stage1, 0,sizeof(lnt_member_process_stage1_CTS_t));
	memset(&stage2, 0,sizeof(lnt_member_process_stage2_STC_t));
	
	dpinit_req.area = 0x01; //广州
	dpinit_req.dtype = 0x00; //普通用户
	dpinit_req.member_op = op;  
	dpinit_req.spno = 0x0101;
	//dpinit_req.spno = 0xffff;
	dpinit_req.member_mark = 0x00;

	if(__lnt_register_stat_get() != LNT_REGISTER_STAT_OK) //判断读卡器是否已经签到
		return LNT_ERROR;

#if 0
	ret = dev_qry_ticket_info(&qry_ticket_ack, &stat, 200);
	if((ret <=  0) && (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "Member Process: [query ticket] UART failed!\n\n");

		proc->proc = LNT_MEMBER_PROC_QUERTICKET;
		proc->fresult = LNT_FRESULT_E_UART;  //串口出错
		proc->stat = stat;
			
		return LNT_ERROR;
	}

	if(qry_ticket_ack.ticket.pmflag == 0x80) //已经是会员卡
	{
		fprintf(stderr, "Member Process: This Card is member!!\n\n");

		proc->proc = LNT_MEMBER_PROC_QUERTICKET;
		proc->fresult = LNT_FRESULT_E_MEMBER;  
		proc->stat = stat;
	
		return LNT_ERROR;
	}
#endif

	ret = dev_r_deposit_init(&dpinit_req, &dpinit_ack, &stat, 500); //押金初始化
	if((ret <=  0) && (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "Member Process: [deposit init] UART failed!\n\n");

		proc->proc = LNT_MEMBER_PROC_DEPOSIT_INIT;
		proc->fresult = LNT_FRESULT_E_UART;
		proc->stat = stat;
			
		return LNT_ERROR;
	}

	proc->proc = LNT_MEMBER_PROC_DEPOSIT_INIT;
	proc->fresult = LNT_FRESULT_E_OK;
	proc->stat = stat;
	
	sockfd = lib_tcp_client_nonb_new((char *)&g_lnt_conf.ipaddr, g_lnt_conf.port, 3);
	if(sockfd < 0)
	{
		fprintf(stderr, "Member Process: [connect LNT server] CONNECT failed!");

		proc->proc = LNT_MEMBER_PROC_CONN;
		proc->fresult = LNT_FRESULT_E_NET;
			
		return LNT_ERROR;
	}

	proc->proc = LNT_MEMBER_PROC_CONN;
	proc->fresult = LNT_FRESULT_E_OK;


	memcpy(stage1.userid, g_lnt_conf.userid, 16);  //用户ID
	stage1.appstep = 0x00;  //应用步骤
	memcpy(&stage1.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&stage1.INFO, &dpinit_ack.data, sizeof(dpinit_ack.data));

	pkt_head.head = LNT_HTONS(LNT_MEMBER_REGISTER);
    	pkt_head.version = 0x01;
    	pkt_head.length = LNT_HTONS(sizeof(lnt_member_process_stage1_CTS_t) - 4);

	stage1.ilen = sizeof(dpinit_ack.data);
	stage1.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage1, sizeof(lnt_member_process_stage1_CTS_t) - 4)); //计算校验
	
	txlen = pkt_head_len + sizeof(lnt_member_process_stage1_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &stage1, sizeof(lnt_member_process_stage1_CTS_t)); //拷贝报文体

#ifdef LNT_MEMBER_PROC_DEBUG
	lib_printf_v2("--------------MEMBER PROCESS DD73---------------", txbuf, txlen, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	ret = lib_tcp_writen(sockfd, txbuf, txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Member Process: [DD73] WRITE failed!\n\n");

		proc->proc = LNT_MEMBER_PROC_DD73;
		proc->fresult = LNT_FRESULT_E_NET;

		lib_disconnect(sockfd);
		return LNT_ERROR;
	}

	proc->proc = LNT_MEMBER_PROC_DD73;
	proc->fresult = LNT_FRESULT_E_OK;

	fprintf(stderr, "Member Process: [DD73] WRITE success\n\n");

	ret = lib_tcp_read_select(sockfd, rxbuf, sizeof(rxbuf), 500);
	if(ret <= 0)
	{
		fprintf(stderr, "Member Process: [DD74] READ failed!\n\n");

		proc->proc = LNT_MEMBER_PROC_DD74;
		proc->fresult = LNT_FRESULT_E_NET;

		lib_disconnect(sockfd);
		return LNT_ERROR;
	}

#ifdef LNT_MEMBER_PROC_DEBUG
	lib_printf_v2("--------------MEMBER PROCESS DD74---------------", rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
	memcpy(&pkt_head, rxbuf, pkt_head_len);
   	memcpy(&stage2, &rxbuf[pkt_head_len], sizeof(lnt_member_process_stage2_STC_t));

	if(stage2.retcode != 0x00) //返回信息码
	{
		fprintf(stderr, "Member Process: [DD74] RETURN CODE failed!\n\n");
			
		proc->proc = LNT_MEMBER_PROC_DD74;
		proc->fresult = LNT_FRESULT_E_RETCODE;  
		proc->stat = stage2.retcode; //报文返回码

		lib_disconnect(sockfd);
		return LNT_ERROR;
	}

	if(stage2.retcode == LNT_CINFO_ERROR_NOT_REGISTER)  //读卡器未签到
	{
		fprintf(stderr, "Member Process: [DD74] CARD NOT REGISTER failed!\n\n");
			
		proc->proc = LNT_MEMBER_PROC_DD74;
		proc->fresult = LNT_FRESULT_E_RETCODE;  
		proc->stat = stage2.retcode; //报文返回码

		lib_disconnect(sockfd);

		__lnt_register_notify_put(LNT_REGISTER_STAT_RE);  //通知签到线程签到
		
		return LNT_ERROR;
	}
	
	crc32 = lnt_crc32((unsigned char *)&stage2, sizeof(lnt_member_process_stage2_STC_t) - 4); //计算校验
	if(LNT_NTOHL(stage2.crc32) != crc32)  //检验码
	{
		fprintf(stderr, "Member Process: [DD74] CRC failed!\n\n");
			
		proc->proc = LNT_MEMBER_PROC_DD74;
		proc->fresult = LNT_FRESULT_E_CRC;
		proc->stat = stage2.retcode; //报文返回码

		lib_disconnect(sockfd);
		return LNT_ERROR;
	}	
	
	if(stage2.indcode != 0xFF) //流程指示码
	{
		fprintf(stderr, "Member Process: [DD74] STEP failed!\n\n");

		proc->proc = LNT_MEMBER_PROC_DD74;
		proc->fresult = LNT_FRESULT_E_STEP;
		proc->stat = stage2.retcode; //报文返回码

		lib_disconnect(sockfd);
		return LNT_ERROR;
	}

	fprintf(stderr, "Member Process: [DD74] READ success\n\n");

#if 0
	/* 判断是否同一张卡执行流程操作 */
	memset(&qry_ticket_ack, 0, sizeof(lnt_qry_ticket_info_ack_t));
	ret = dev_qry_ticket_info(&qry_ticket_ack, &stat, 200);
	if((ret <=  0) && (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "Member Process: [query ticket] UART failed!\n\n");

		proc->proc = LNT_MEMBER_PROC_QUERTICKET;
		proc->fresult = LNT_FRESULT_E_UART;  //串口出错
		proc->stat = stat;
			
		return LNT_ERROR;
	}

	if(memcmp(qry_ticket_ack.ticket.pid, pid, 8) != 0) //判断是否同一张卡
	{
		fprintf(stderr, "Member Process: [CMP PID] UART failed!\n\n");

		proc->proc = LNT_MEMBER_PROC_QUERTICKET;
		proc->fresult = LNT_FRESULT_E_PID; 
		proc->stat = stat;
			
		return LNT_ERROR;
	}
#endif

	memcpy(&dproc_req.data, &stage2.INFO, sizeof(lnt_r_deposit_process_req_t));
	ret = dev_r_deposit_process(&dproc_req, &dproc_ack, &stat, 500);
	if((ret <=  0) && (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "deposit process failed!\n");
		
		proc->proc = LNT_MEMBER_PROC_DEPOSIT_PROCESS;
		proc->fresult = LNT_FRESULT_E_UART;
		proc->stat = stat;
			
		return LNT_ERROR;
	}

	proc->proc = LNT_MEMBER_PROC_DEPOSIT_PROCESS;
	proc->fresult = LNT_FRESULT_E_OK;
	proc->stat = stat;
	
	lib_disconnect(sockfd);

	return LNT_EOK;
}
#endif

#if 0
/*
 * 开卡处理
 */
int lib_lnt_on_card_process(lnt_agent_on_card_stage1_CTS_t *stage1, lnt_member_proc_code_t *proc, fn_agent_proc fn_agent)
{
	if((stage1 == NULL) || (proc == NULL))
		goto Done;

	int ret = LNT_ERROR;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	int sockfd = -1;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr *agent_ptr = (struct agent_pkt_ptr *)lib_zmalloc(sizeof(struct agent_pkt_ptr));
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;

	
	sockfd = lib_tcp_client_nonb_new((char *)&g_lnt_conf.ipaddr, g_lnt_conf.port, 3);
	if(sockfd < 0)
	{
		fprintf(stderr, "On Card Process: [connect AGENT server] CONNECT failed!");
			
		return LNT_ERROR;
	}

	fprintf(stderr, "On Card sockfd = %d\n", sockfd);

	/* 阶段1(会员申请请求) */
	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_ON_CARD_STAGE1, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_data_set(AGENT_WAY_COM, stage1, sizeof(lnt_agent_on_card_stage1_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_ON_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT ON CARD PROCESS BB01---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] WRITE failed!\n\n", AGENT_ON_CARD_STAGE1);

		lib_disconnect(sockfd);
		return LNT_ERROR;
	}

	fprintf(stderr, "On Card Process: [%02x] WRITE success\n\n", AGENT_ON_CARD_STAGE1);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), 5000);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] READ failed!\n\n", AGENT_ON_CARD_STAGE2);

		

		lib_disconnect(sockfd);
		return LNT_ERROR;
	}

#ifdef LNT_ON_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT ON CARD PROCESS BB02---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_on_card_stage2_STC_t agent_stage2_STC;
	memset(&agent_stage2_STC, 0, sizeof(lnt_agent_on_card_stage2_STC_t));
	agent_ptr->exp_ptr = &agent_rxbuf;
	agent_ptr->const_exp_sz = ret;
	agent_ptr->extra_ptr = NULL;
	agent_ptr->extra_sz = 0;
	agent_ptr->data_ptr = &agent_stage2_STC;
	agent_ptr->data_sz = sizeof(lnt_agent_on_card_stage2_STC_t);
	agent_err = __agent_pkt_data_on_card_explain(agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{

		goto Done;
	}

	if(fn_agent != NULL)
		fn_agent(AGENT_ON_CARD_STAGE2, &agent_stage2_STC, agent_ptr->data_sz);
	
	








	fprintf(stderr, "Lnt On Card Process OK\n");
	if(sockfd > 0)
		lib_disconnect(sockfd);
	lib_zfree(agent_ptr);
	return LNT_EOK;

Done:
	fprintf(stderr, "Lnt On Card Process Failed!\n");
	if(sockfd > 0)
		lib_disconnect(sockfd);
	lib_zfree(agent_ptr);
	return LNT_ERROR;
}
#endif


int lib_lnt_is_same_card(const unsigned char pid[8])
{
	int ret = LNT_ERROR;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_qry_ticket_info_ack_t qry_ticket;

	memset(&qry_ticket, 0, sizeof(lnt_qry_ticket_info_ack_t));
	ret = dev_qry_ticket_info(&qry_ticket, &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		return LNT_FALSE;
	}

	if(memcmp(pid, qry_ticket.ticket.pid, 8) == 0)  //同一张卡片
		return LNT_TRUE;

	return LNT_FALSE;
}

#if 1
int lib_lnt_consume_and_get_record(lib_lnt_consume_req_t *consume, lib_lnt_trade_record_ack_t *record, unsigned char *stat, unsigned char *f_type)
{
		if((consume == NULL) || (record == NULL))
			goto Done;

		int ret = LNT_ERROR, i = 0;
		unsigned char mstat = LNT_MOC_ERROR_UNKNOW;


		//加入消费失败后的处理流程 2016-03-23 by zjc
		for(i = 0; i < LNT_CONSUME_TRY_TIMES; i++)
		{
			fprintf(stderr,"Consume Amount: %2d.%02d(Y), Fee: %2d.%02d(Y)\n", consume->amount / 100,consume->amount % 100, consume->fee / 100,consume->fee % 100);

			//消费
			ret = lib_lnt_consume(consume, &mstat, 800);		
			if((ret <=  0) || (mstat != LNT_MOC_ERROR_NORMAL))	
			{
				printf("On Card Process: [Consume] Status: 0x%02X\n",mstat);

				if(stat != NULL)
					*stat = mstat;
				
				if(f_type != NULL)
					*f_type = LNT_CONSUME;
				
				//lib_msleep(100);
			#if 1
				//未完整交易流程
				if(i != (LNT_CONSUME_TRY_TIMES - 1))
				{	
					//查询票卡信息
					lnt_qry_ticket_info_ack_t qry_ticket;  
					unsigned char stat = LNT_MOC_ERROR_UNKNOW;
					memset(&qry_ticket, 0, sizeof(lnt_qry_ticket_info_ack_t));

					ret = lib_lnt_qry_ticket_info(&qry_ticket, &stat, 500);
					if((ret < 0) || (stat != LNT_MOC_ERROR_NORMAL)) 
					{
						printf("On Card Process: [qry_ticket] Status: 0x%02X\n",stat);
						goto Done;
					}
					
					//lib_msleep(100);
					//查询租还车信息
					lnt_rent_info_req_t rent_info_req;
					lnt_rent_info_ack_t rent_info_ack;
					memset(&rent_info_req, 0, sizeof(lnt_rent_info_req_t));
					memset(&rent_info_ack, 0, sizeof(lnt_rent_info_ack_t));

					stat = LNT_MOC_ERROR_UNKNOW;
					rent_info_req.id = 0x68;
					ret = lib_lnt_qry_rent_info(&rent_info_req, &rent_info_ack, &stat, 500);
					if((ret < 0) || (stat != LNT_MOC_ERROR_NORMAL)) 
					{
						printf("On Card Process: [qry_rent] Status: 0x%02X\n",stat);
						goto Done;
					}
					
					//lib_msleep(100);
					//设置租还车信息
					lib_lnt_rent_info_t rent_info;
					memset(&rent_info, 0, sizeof(lib_lnt_rent_info_t));

					stat = LNT_MOC_ERROR_UNKNOW;
					rent_info.id = 0x68;
					rent_info.length = sizeof(lib_rent_info_t);
					memcpy(&rent_info.rent, &rent_info_ack.rent, sizeof(rent_info_ack.rent));
					
					lib_lnt_set_rent_info(&rent_info, &stat, 500);
					if((ret < 0) || (stat != LNT_MOC_ERROR_NORMAL))
					{
						printf("On Card Process: [set_rent] Status: 0x%02X\n",stat);
						goto Done;
					}
					
					//lib_msleep(100);
					continue;	
				}
				else
				{
					goto Done;	//尝试多次后还是失败
				}
			#endif
			}
			
			break;//消费成功
		}

		//消费成功后获取交易记录
		ret = LNT_ERROR;
		mstat = LNT_MOC_ERROR_UNKNOW;
		ret = lib_lnt_get_trade_record(record, &mstat, 300);
		if((ret <=  0) || (mstat != LNT_MOC_ERROR_NORMAL))
		{
			if(stat != NULL)
				*stat = mstat;

			if(f_type != NULL)
				*f_type = LNT_GET_TRANS;
			
			goto Done;
		}

		if(stat != NULL)
			*stat = mstat;
		
		if(f_type != NULL)
			*f_type = 0x00;
		
		return LNT_EOK;

Done:
	return LNT_ERROR;
}

#else
int lib_lnt_consume_and_get_record(lib_lnt_consume_req_t *consume, lib_lnt_trade_record_ack_t *record, unsigned char *stat, unsigned char *f_type)
{
	if((consume == NULL) || (record == NULL))
		goto Done;

	int ret = LNT_ERROR, i = 0;
	unsigned char mstat = LNT_MOC_ERROR_UNKNOW;

	//加入消费失败后的处理流程 2016-03-23 by zjc
	for(i = 0; i < LNT_CONSUME_TRY_TIMES; i++)
	{
		fprintf(stderr,"Consume Amount: %d, Fee: %d\n",consume->amount,consume->fee);
		ret = lib_lnt_consume(consume, &mstat, 600);
		printf("On Card  Process: [CONSUME] Status: %02X\n",mstat);
		if((ret <=  0) || (mstat != LNT_MOC_ERROR_NORMAL)) //消费失败后走未完整交易流程
		{
			if(stat != NULL)
				*stat = mstat;
			if(f_type != NULL)
				*f_type = LNT_CONSUME;
			#if 0	
			if(mstat == 0xB0) //返回B0则会扣费成功!
				goto Done;
			#endif
			
			lib_msleep(100);
			if(i != LNT_CONSUME_TRY_TIMES)
			{	
				//查询票卡信息
				lnt_qry_ticket_info_ack_t qry_ticket;  
				unsigned char stat = LNT_MOC_ERROR_UNKNOW;
				memset(&qry_ticket, 0, sizeof(lnt_qry_ticket_info_ack_t));

				ret = lib_lnt_qry_ticket_info(&qry_ticket, &stat, 500);
				if((ret < 0) || (stat != LNT_MOC_ERROR_NORMAL)) 
				{
					fprintf(stderr,"qry_ticket_info failed!\n");
					goto Done;
				}
				
				lib_msleep(100);			
				continue;	
			}
			else //尝试多次失败则退出
			{
				#if 0
				if(stat != NULL)
					*stat = mstat;
				if(f_type != NULL)
					*f_type = LNT_CONSUME;
				#endif
				goto Done;	
			}
		}
		
		//printf("Consume Success ...\n");
		break; //消费成功则跳出循环进入获取交易记录
	}

	ret = LNT_ERROR;
	mstat = LNT_MOC_ERROR_UNKNOW;
	ret = lib_lnt_get_trade_record(record, &mstat, 500);
	if((ret <=  0) || (mstat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr,"get_trade_record failed!\n");

		if(stat != NULL)
			*stat = mstat;
		if(f_type != NULL)
			*f_type = LNT_GET_TRANS;
		
		goto Done;
	}

	if(stat != NULL)
		*stat = mstat;
	if(f_type != NULL)
		*f_type = 0x00;
	
	return LNT_EOK;

Done:
	return LNT_ERROR;
}
#endif

int lib_lnt_on_card_connect_to_agent(void)
{
	int sockfd = lib_tcp_client_nonb_new((char *)&g_lnt_conf.ipaddr, g_lnt_conf.port, 3);
	if(sockfd < 0)
	{
		fprintf(stderr, "On Card Process: [connect AGENT server] CONNECT failed!");
		return LNT_ERROR;
	}

	lib_setfd_noblock(sockfd); //设置为非阻塞

	fprintf(stderr, "On Card sockfd = %d\n", sockfd);
	return sockfd;
}

void lib_lnt_on_card_close_to_agent(int sockfd)
{
	fprintf(stderr, "On Card Close sockfd = %d\n", sockfd);
	
	if(sockfd > 0)
		lib_close(sockfd);
}

unsigned int lib_pkt_sn_RO_get(void)
{
	return g_lnt_conf.pkt_sn_RO;
}

enum AGENT_ERR lib_lnt_on_card_stage1TO2_process(const int sockfd, lnt_agent_on_card_stage1_CTS_t *stage1, lnt_agent_on_card_stage2_STC_t *stage2)
{
	if((stage1 == NULL) || (stage2 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&stage1->pki, &g_lnt_conf.pki_RO, 4);  //PKI

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_ON_CARD_STAGE1, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_data_set(AGENT_WAY_COM, stage1, sizeof(lnt_agent_on_card_stage1_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_ON_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT ON CARD PROCESS BB01---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] WRITE failed!\n\n", AGENT_ON_CARD_STAGE1);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "On Card Process: [%02x] WRITE success\n\n", AGENT_ON_CARD_STAGE1);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] READ failed!\n\n", AGENT_ON_CARD_STAGE2);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_ON_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT ON CARD PROCESS BB02---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage2;
	agent_ptr.data_sz = sizeof(lnt_agent_on_card_stage2_STC_t);
	agent_err = __agent_pkt_data_on_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "On Card stage2 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;
}

 enum AGENT_ERR lib_lnt_on_card_stage3TO4_process(const int sockfd, lnt_agent_on_card_stage3_CTS_t *stage3, lnt_agent_on_card_stage4_STC_t *stage4,  agent_retransmission_data_t *retrans)
{
	if((stage3 == NULL) || (stage4 == NULL) || (retrans == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&stage3->pki, &g_lnt_conf.pki_RO, 4);  //PKI

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_ON_CARD_STAGE3, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, stage3, sizeof(lnt_agent_on_card_stage3_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

	/* 保留重传数据 */
	retrans->cmd = LNT_HTONS(AGENT_ON_CARD_STAGE3);
	memcpy(retrans->pid, stage3->pid, 8);
	retrans->local_pkt_sn = g_lnt_conf.pkt_sn_RO; //交易流水号->报文序列号
	memcpy(&retrans->pkt_data, &agent_txbuf, agent_txlen);
	retrans->pkt_len = agent_txlen;

//goto Done; //test for retrans

#ifdef LNT_ON_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT ON CARD PROCESS BB10---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] WRITE failed!\n\n", AGENT_ON_CARD_STAGE3);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "On Card Process: [%02x] WRITE success\n\n", AGENT_ON_CARD_STAGE3);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] READ failed!\n\n", AGENT_ON_CARD_STAGE4);

		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA; //接收不到数据
	}

#ifdef LNT_ON_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT ON CARD PROCESS BB11---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage4;
	agent_ptr.data_sz = sizeof(lnt_agent_on_card_stage4_STC_t);
	agent_err = __agent_pkt_data_on_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "On Card stage4 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_on_card_stage5TO6_process(const int sockfd, agent_extra_data_t *extra, lnt_member_process_stage2_STC_t *member_stage2, unsigned char *stat)
{
	if((extra == NULL) || (member_stage2 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	unsigned int crc32 = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	//unsigned char mstat = LNT_MOC_ERROR_CARD_OP_FAIL;
	unsigned char mstat = LNT_MOC_ERROR_UNKNOW;
	lnt_r_deposit_init_req_t dpinit_req;
	lnt_r_deposit_init_ack_t dpinit_ack;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	lnt_agent_on_card_stage6_STC_t agent_stage6;
	lnt_member_process_stage1_CTS_t member_stage1;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&dpinit_req, 0, sizeof(lnt_r_deposit_init_req_t));
	memset(&dpinit_ack, 0, sizeof(lnt_r_deposit_init_ack_t));
	memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
	memset(&agent_stage6, 0, sizeof(lnt_agent_on_card_stage6_STC_t));
	memset(&member_stage1, 0, sizeof(lnt_member_process_stage1_CTS_t));
	
	dpinit_req.area = 0x01; //广州
	dpinit_req.dtype = 0x00;  //普通用户
	dpinit_req.member_op = LNT_MEMBER_OP_REGISTER;  //开卡
	dpinit_req.spno = 0x0101; //一定要0x0101,不然DD74的返回信息码是0xEE
	dpinit_req.member_mark = 0x00;  
	
	ret = dev_r_deposit_init(&dpinit_req, &dpinit_ack, &mstat, 500); //押金初始化
	if((ret <=  0) || (mstat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "On Card Process: [deposit init] stat:0x%02X\n\n",mstat);	
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}
	#endif
	
		if(stat != NULL)
			*stat = mstat;
		
		fprintf(stderr, "On Card  Process: [deposit init] UART failed!\n\n");	
		goto Done;
	}

#if USE_NEW_USER_ID
	memcpy(&member_stage1.userid[16 - UID_LEN],USER_ID,UID_LEN);
#else
	memcpy(member_stage1.userid, g_lnt_conf.userid, 16);  //用户ID
#endif
	//lib_printf("member_stage1.userid",&member_stage1.userid,16);

	member_stage1.appstep = 0x00;  //应用步骤
	memcpy(&member_stage1.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&member_stage1.INFO, &dpinit_ack.data, sizeof(dpinit_ack.data));

	pkt_head.head = LNT_HTONS(LNT_MEMBER_REGISTER);
    	pkt_head.version = 0x01;
    	pkt_head.length = LNT_HTONS(sizeof(lnt_member_process_stage1_CTS_t) - 4);

	member_stage1.ilen = sizeof(dpinit_ack.data);
	member_stage1.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&member_stage1, sizeof(lnt_member_process_stage1_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_member_process_stage1_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &member_stage1, sizeof(lnt_member_process_stage1_CTS_t)); //拷贝报文体
	
	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_ON_CARD_STAGE5, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, extra->macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_ON_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT ON CARD PROCESS DD73---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] WRITE failed!\n\n", AGENT_ON_CARD_STAGE5);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "On Card Process: [%02x] WRITE success\n\n", AGENT_ON_CARD_STAGE5);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] READ failed!\n\n", AGENT_ON_CARD_STAGE6);

		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_ON_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT ON CARD PROCESS DD74---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&agent_stage6;
	agent_ptr.data_sz = sizeof(lnt_agent_on_card_stage6_STC_t);
	agent_err = __agent_pkt_data_on_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "On Card stage6 Explain Error!\n");
		return agent_err;
	}

	memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
	memcpy(&pkt_head, agent_stage6.agent_info, pkt_head_len);
   	memcpy(member_stage2, &agent_stage6.agent_info[pkt_head_len], sizeof(lnt_member_process_stage2_STC_t));

#if 0
	if(member_stage2->retcode != 0x00) //返回信息码
	{
		fprintf(stderr, "On Card Process: [DD74] RETURN CODE failed!\n\n");
		
		lib_disconnect(sockfd);
		goto Done;
	}
#endif
	printf("On Card Process: [DD74] retcode: 0x%02X\n",member_stage2->retcode);

	//if(member_stage2->retcode != 0x00)  
	if(member_stage2->retcode == 0xBB)  
	{
		printf("On Card Process: [DD74] retcode: %02X\n",member_stage2->retcode);

		fprintf(stderr, "On Card Process: [DD74] CARD NOT REGISTER failed!\n\n");
			
		__lnt_register_notify_put(LNT_REGISTER_STAT_RE);  //通知签到线程签到

		lib_disconnect(sockfd);
		return member_stage2->retcode;
	}

	crc32 = lnt_crc32((unsigned char *)member_stage2, sizeof(lnt_member_process_stage2_STC_t) - 4); //计算校验
	if(LNT_NTOHL(member_stage2->crc32) != crc32)  //检验码
	{
		fprintf(stderr, "On Card Process: [DD74] CRC failed!\n\n");
		
		lib_disconnect(sockfd);
		return AGENT_ERR_CRC;
	}	

	#if 0
	if(member_stage2->indcode != 0xFF) //流程指示码
	{
		fprintf(stderr, "On Card Process: [DD74] STEP failed!\n\n");

		lib_disconnect(sockfd);
		goto Done;
	}
	#endif
	
	fprintf(stderr, "On Card Process: [DD74] READ success\n\n");

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_on_card_stage7TO8_process(const int sockfd, agent_extra_data_t *extra, lnt_agent_on_card_stage8_STC_t *stage8, agent_retransmission_data_t *retrans)
{
	if((extra == NULL) || (stage8 == NULL) || (retrans == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	lnt_agent_on_card_stage7_CTS_t agent_stage7;

	memset(&agent_stage7, 0, sizeof(lnt_agent_on_card_stage7_CTS_t));
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&agent_stage7.pid, &extra->pid, 8); 	//PID 
	memcpy(&agent_stage7.macaddr, &g_lnt_conf.macaddr, 6);  //MAC
	memcpy(&agent_stage7.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&agent_stage7.pkt_sn, &extra->local_pkt_sn, 4); 	//PKT SN
	memcpy(&agent_stage7.indcode, &extra->indcode, 8); 	 //流程指示码
	agent_stage7.result = extra->result;		//结果
	lib_lnt_utils_time_bcd_yymmddhhMMss(agent_stage7.time);  //时间
	
	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_ON_CARD_STAGE7, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, &agent_stage7, sizeof(lnt_agent_on_card_stage7_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

	/* 保留重传数据 */
	retrans->cmd = LNT_HTONS(AGENT_ON_CARD_STAGE7);
	memcpy(retrans->pid, agent_stage7.pid, 8);
	retrans->local_pkt_sn = g_lnt_conf.pkt_sn_RO;  //交易流水号->报文序列号
	memcpy(&retrans->pkt_data, &agent_txbuf, agent_txlen);
	retrans->pkt_len = agent_txlen;

//goto Done; //test for retrans

#ifdef LNT_ON_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT ON CARD PROCESS BB20---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] WRITE failed!\n\n", AGENT_ON_CARD_STAGE7);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "On Card Process: [%02x] WRITE success\n\n", AGENT_ON_CARD_STAGE7);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card Process: [%02x] READ failed!\n\n", AGENT_ON_CARD_STAGE8);

		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_ON_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT ON CARD PROCESS BB21---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage8;
	agent_ptr.data_sz = sizeof(lnt_agent_on_card_stage8_STC_t);
	agent_err = __agent_pkt_data_on_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "On Card stage8 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}


/**************************************销卡函数***************************************/


enum AGENT_ERR lib_lnt_off_card_stage1TO2_process(const int sockfd, lnt_agent_off_card_stage1_CTS_t *stage1, lnt_agent_off_card_stage2_STC_t *stage2, agent_extra_data_t *extra)
{
	if((stage1 == NULL) || (stage2 == NULL) || (extra == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	unsigned short agent_ilen = 0;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&stage1->pki, &g_lnt_conf.pki_RO, 4);  //PKI

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE1, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_data_set(AGENT_WAY_COM, stage1, sizeof(lnt_agent_off_card_stage1_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS BB03---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE1);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE1);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE2);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS BB04---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage2;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage2_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage2 Explain Error! agent_err = %02X\n", agent_err);
		return agent_err;
	}

	agent_ilen = LNT_NTOHS(stage2->agent_ilen);  //数据长度
	if(agent_ilen == 0)  //数据长度为0
		return AGENT_ERR_OK;

	if(stage2->agent_info) //发往读卡器的指令不为空  2016-03-07 zjc
	{
		agent_toreader_array_t toreader;
		memset(&toreader, 0, sizeof(agent_toreader_array_t));

		memcpy(&toreader, stage2->agent_info, agent_ilen);
		memcpy(extra->toreader_pki, toreader.pki, 4);
		memcpy(extra->toreader_serial, toreader.serial, 8);

		ret = __apdu_process(&(extra->str), toreader.toreader_array, &extra->r_stat); //APDU处理
		if(ret != LNT_EOK)
			goto Done;
	}
	
	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_off_card_stage3TO4_process(const int sockfd, agent_extra_data_t *extra, lnt_agent_off_card_stage4_STC_t *stage4)
{
	if((extra == NULL) || (stage4 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[1024] = {0};
	unsigned char agent_rxbuf[1204] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned short agent_ilen =  LNT_HTONS(extra->str.s_len + 13);
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE3, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, g_lnt_conf.pki_RO, 4);  //PKI

	__agent_pkt_data_set(AGENT_WAY_COM, &agent_ilen, 2); //信息长度
	__agent_pkt_data_set(AGENT_WAY_COM, extra->toreader_pki, 4);  //toreader PKI
	__agent_pkt_data_set(AGENT_WAY_COM, extra->toreader_serial, 8); //toreader serial
	__agent_pkt_char_set(AGENT_WAY_COM, 0x00); //step
	__agent_pkt_data_set(AGENT_WAY_COM, extra->str.s_str, extra->str.s_len);  //string

	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS BB30---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE3);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE3);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms); 
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE4);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS BB31---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage4;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage4_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage4 Explain Error!\n");
		return agent_err;
	}

	agent_ilen = LNT_NTOHS(stage4->agent_ilen);  //数据长度
	if(agent_ilen == 0)  //数据长度为0
		goto Done;
	
	agent_toreader_array_t toreader;
	memset(&toreader, 0, sizeof(agent_toreader_array_t));
	memcpy(&toreader, stage4->agent_info, agent_ilen);
	memcpy(extra->toreader_pki, toreader.pki, 4);  //用最新的PKI和SERIAL
	memcpy(extra->toreader_serial, toreader.serial, 8);

	memset(&(extra->str), 0, sizeof(extra->str));
	ret = __apdu_process(&(extra->str), toreader.toreader_array, &extra->r_stat); //APDU处理
	if(ret != LNT_EOK)
		goto Done;

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_off_card_stage5TO6_process(const int sockfd, agent_extra_data_t *extra, lnt_agent_off_card_stage6_STC_t *stage6)
{
	if((extra == NULL) || (stage6 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[1024] = {0};
	unsigned char agent_rxbuf[1204] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned short agent_ilen =  LNT_HTONS(extra->str.s_len + 13);
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE5, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, g_lnt_conf.pki_RO, 4);  //PKI

	__agent_pkt_data_set(AGENT_WAY_COM, &agent_ilen, 2); //信息长度
	__agent_pkt_data_set(AGENT_WAY_COM, extra->toreader_pki, 4);  //toreader PKI
	__agent_pkt_data_set(AGENT_WAY_COM, extra->toreader_serial, 8); //toreader serial
	__agent_pkt_char_set(AGENT_WAY_COM, 0x01); //step
	__agent_pkt_data_set(AGENT_WAY_COM, extra->str.s_str, extra->str.s_len);  //string

	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS BB32---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE5);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE5);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE6);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS BB33---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage6;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage6_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage6 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;		
}

enum AGENT_ERR lib_lnt_off_card_stage7TO8_process(const int sockfd, agent_extra_data_t *extra, lnt_member_process_stage2_STC_t *member_stage2)
{
	if((extra == NULL) || (member_stage2 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	unsigned int crc32 = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	//unsigned char mstat = LNT_MOC_ERROR_CARD_OP_FAIL;
	unsigned char mstat = LNT_MOC_ERROR_UNKNOW;
	lnt_r_deposit_init_req_t dpinit_req;
	lnt_r_deposit_init_ack_t dpinit_ack;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	lnt_agent_off_card_stage8_STC_t agent_stage8;
	lnt_member_process_stage1_CTS_t member_stage1;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&dpinit_req, 0, sizeof(lnt_r_deposit_init_req_t));
	memset(&dpinit_ack, 0, sizeof(lnt_r_deposit_init_ack_t));
	memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
	memset(&agent_stage8, 0, sizeof(lnt_agent_off_card_stage8_STC_t));
	memset(&member_stage1, 0, sizeof(lnt_member_process_stage1_CTS_t));
	
	dpinit_req.area = 0x01; //广州
	dpinit_req.dtype = 0x00;  //普通用户
	dpinit_req.member_op = LNT_MEMBER_OP_CANCEL;  //销卡
	dpinit_req.spno = 0x0101; //一定要0x0101,不然DD74的返回信息码是0xEE
	dpinit_req.member_mark = 0x00;  
	
	ret = dev_r_deposit_init(&dpinit_req, &dpinit_ack, &mstat, 600); //押金初始化
	if((ret <=  0) || (mstat != LNT_MOC_ERROR_NORMAL))
	{	
		fprintf(stderr, "Off Card Process: [deposit init] stat:0x%02X\n\n",mstat);	
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}
	#endif
	
		extra->r_stat = mstat;
		
		fprintf(stderr, "Off Card  Process: [deposit init] UART failed!\n\n");	
		goto Done;
	}

#if USE_NEW_USER_ID
	memcpy(&member_stage1.userid[16 - UID_LEN],USER_ID,UID_LEN);
#else
	memcpy(member_stage1.userid, g_lnt_conf.userid, 16);  //用户ID
#endif
	//lib_printf("member_stage1.userid",&member_stage1.userid,16);

	member_stage1.appstep = 0x00;  //应用步骤
	memcpy(&member_stage1.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&member_stage1.INFO, &dpinit_ack.data, sizeof(dpinit_ack.data));

	pkt_head.head = LNT_HTONS(LNT_MEMBER_REGISTER);
    	pkt_head.version = 0x01;
    	pkt_head.length = LNT_HTONS(sizeof(lnt_member_process_stage1_CTS_t) - 4);

	member_stage1.ilen = sizeof(dpinit_ack.data);
	member_stage1.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&member_stage1, sizeof(lnt_member_process_stage1_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_member_process_stage1_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &member_stage1, sizeof(lnt_member_process_stage1_CTS_t)); //拷贝报文体
	
	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_ON_CARD_STAGE5, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, extra->macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS DD73---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE7);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE7);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE8);

		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS DD74---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&agent_stage8;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage8_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage8 Explain Error!\n");
		return agent_err;
	}

	memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
	memcpy(&pkt_head, agent_stage8.agent_info, pkt_head_len);
   	memcpy(member_stage2, &agent_stage8.agent_info[pkt_head_len], sizeof(lnt_member_process_stage2_STC_t));

	//printf("\n----------[DD74] retcode:%02X----------\n",member_stage2->retcode);
	if(member_stage2->retcode != 0x00) //by zjc at 2016-10-31
	{
		fprintf(stderr, "Off Card Process: [DD74] RETURN CODE failed!\n\n");

		if(member_stage2->retcode == 0xBB)  
		{
			fprintf(stderr, "Off Card Process: [DD74] CARD NOT REGISTER failed!\n\n");
				
			__lnt_register_notify_put(LNT_REGISTER_STAT_RE);  //通知签到线程签到

			//lib_disconnect(sockfd);
			//return member_stage2->retcode;
		}
		
		lib_disconnect(sockfd);
		return member_stage2->retcode;
	}
	
	crc32 = lnt_crc32((unsigned char *)member_stage2, sizeof(lnt_member_process_stage2_STC_t) - 4); //计算校验
	if(LNT_NTOHL(member_stage2->crc32) != crc32)  //检验码
	{
		fprintf(stderr, "Off Card Process: [DD74] CRC failed!\n\n");
		
		lib_disconnect(sockfd);
		return AGENT_ERR_CRC;
	}	

#if 0	
	if(member_stage2->indcode != 0xFF) //流程指示码
	{
		fprintf(stderr, "Off Card Process: [DD74] STEP failed!\n\n");

		lib_disconnect(sockfd);
		goto Done;
	}
#endif

	fprintf(stderr, "Off Card Process: [DD74] READ success\n\n");

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_off_card_stage9TO10_process(const int sockfd, agent_extra_data_t *extra, lnt_agent_off_card_stage10_STC_t *stage10, agent_retransmission_data_t *retrans)
{
	if((extra == NULL) || (stage10 == NULL) || (retrans == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	lnt_agent_off_card_stage9_CTS_t agent_stage9;

	memset(&agent_stage9, 0, sizeof(lnt_agent_off_card_stage9_CTS_t));
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&agent_stage9.pid, &extra->pid, 8); 	//PID 
	memcpy(&agent_stage9.macaddr, &g_lnt_conf.macaddr, 6);  //MAC
	memcpy(&agent_stage9.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&agent_stage9.pkt_sn, &extra->local_pkt_sn, 4); 	//PKT SN
	memcpy(&agent_stage9.indcode, &extra->indcode, 8); 	 //流程指示码
	agent_stage9.result = extra->result;		//结果
	lib_lnt_utils_time_bcd_yymmddhhMMss(agent_stage9.time);  //时间
	
	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE9, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, &agent_stage9, sizeof(lnt_agent_off_card_stage9_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

	/* 保留重传数据 */
	retrans->cmd = LNT_HTONS(AGENT_OFF_CARD_STAGE9);
	memcpy(retrans->pid, agent_stage9.pid, 8);
	retrans->local_pkt_sn = g_lnt_conf.pkt_sn_RO;  //交易流水号->报文序列号
	memcpy(&retrans->pkt_data, &agent_txbuf, agent_txlen);
	retrans->pkt_len = agent_txlen;

//goto Done; //test for retrans

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS BB41---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE9);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE9);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE10);

		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS BB42---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage10;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage10_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage10 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;
}


enum AGENT_ERR lib_lnt_off_card_stage11TO12_process(const int sockfd, agent_extra_data_t *extra, recharge_info_t *recharge,  lnt_packet_recharge_stage2_STC_t *recharge_stage2)
{
	if((extra == NULL) || (recharge == NULL) || (recharge_stage2 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	lnt_packet_recharge_stage1_CTS_t recharge_stage1_CTS;
		
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&recharge_stage1_CTS, 0, sizeof(lnt_packet_recharge_stage1_CTS_t));


	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE1);
    	pkt_head.version = 0x01;
    	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage1_CTS_t) - 4);

	recharge_stage1_CTS.ilen = sizeof(recharge_info_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&recharge_stage1_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(&recharge_stage1_CTS.userid, 0x00, 16); //UID
#endif
	recharge_stage1_CTS.appstep = 0x00; //STEP
	memcpy(&recharge_stage1_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&recharge_stage1_CTS.INFO, recharge, sizeof(recharge_stage1_CTS.INFO));  
	recharge_stage1_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&recharge_stage1_CTS, sizeof(lnt_packet_recharge_stage1_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage1_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &recharge_stage1_CTS, sizeof(lnt_packet_recharge_stage1_CTS_t)); //拷贝报文体

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE11, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS DD11---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE11);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE11);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE12);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS DD12---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage12_STC_t stage12_STC;
	memset(&stage12_STC, 0, sizeof(lnt_agent_off_card_stage12_STC_t));

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage12_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage12_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);

	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage12 Explain Error!\n");
		
		#if 1
		if(agent_err == AGENT_ERR_CINFO_ERROR_NOT_REGISTER)	
		{
			fprintf(stderr, "Off Card Process: [DD12] CARD NOT REGISTER failed!\n\n");
				
			__lnt_register_notify_put(LNT_REGISTER_STAT_RE);  //通知签到线程签到
	
			lib_disconnect(sockfd);
		}
		#endif
		
		return agent_err;
	}
	
	memcpy(recharge_stage2, &(stage12_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage2_STC_t));

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_off_card_stage13TO14_process(const int sockfd, agent_extra_data_t *extra,  lnt_packet_recharge_stage2_STC_t *recharge_stage2, lnt_packet_recharge_stage4_STC_t *recharge_stage4)
{
	if((extra == NULL) || (recharge_stage2 == NULL) || (recharge_stage4 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_pub_qry_physics_info_Encry_ack_t phy_info_encry;//加密
	lnt_packet_recharge_stage3_CTS_t recharge_stage3;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&phy_info_encry, 0, sizeof(lnt_r_pub_qry_physics_info_Encry_ack_t));
	memset(&recharge_stage3, 0, sizeof(lnt_packet_recharge_stage3_CTS_t));

	ret = dev_CLA_INS_pkg(recharge_stage2->INFO, recharge_stage2->ilen, &phy_info_encry, sizeof(lnt_r_pub_qry_physics_info_Encry_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}else{
			//fprintf(stderr,"lnt_init success!\n");
		}
	#endif
	
		extra->r_stat = stat;
		
		fprintf(stderr, "Off Card  Process: [R_PUB_QRY_PHYSICS_INFO_ENCRY] UART failed!\n\n");	
		goto Done;
	}

	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE3);
    	pkt_head.version = 0x01;
    	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage3_CTS_t) - 4);

	recharge_stage3.ilen = sizeof(lnt_r_pub_qry_physics_info_Encry_ack_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&recharge_stage3.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(&recharge_stage3.userid, 0x00, 16); //UID
#endif
	recharge_stage3.appstep = 0x01; //STEP
	memcpy(&recharge_stage3.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&recharge_stage3.INFO.Encry_INFO, &phy_info_encry, sizeof(recharge_stage3.INFO.Encry_INFO));  
	recharge_stage3.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&recharge_stage3, sizeof(lnt_packet_recharge_stage3_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage3_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &recharge_stage3, sizeof(lnt_packet_recharge_stage3_CTS_t)); //拷贝报文体

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE13, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS DD13---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE13);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE13);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE14);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS DD14---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage14_STC_t stage14_STC;
	memset(&stage14_STC, 0, sizeof(lnt_agent_off_card_stage14_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage14_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage14_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		if(agent_err == 0xBB)  //add by zjc 2016-03-21
		{
			fprintf(stderr, "Off Card Process: [DD14] CARD NOT REGISTER failed!\n\n");
				
			__lnt_register_notify_put(LNT_REGISTER_STAT_RE);  //通知签到线程签到

			lib_disconnect(sockfd);
			return agent_err;
		}
		
		fprintf(stderr, "Off Card stage14 Explain Error!\n");
		return agent_err;
	}

	memcpy(recharge_stage4, &(stage14_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage4_STC_t));
	
	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;
}

enum AGENT_ERR lib_lnt_off_card_stage15TO16_process(const int sockfd, agent_extra_data_t *extra, lnt_packet_recharge_stage4_STC_t *recharge_stage4, lnt_packet_recharge_stage6_STC_t *recharge_stage6)
{
	if((extra == NULL) || (recharge_stage4 == NULL) || (recharge_stage6 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_pub_set_readcardinfo_ack_t readcardinfo_ack;
	lnt_packet_recharge_stage5_CTS_t stage5_CTS;
	
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&readcardinfo_ack, 0, sizeof(lnt_r_pub_set_readcardinfo_ack_t));
	memset(&stage5_CTS, 0, sizeof(lnt_packet_recharge_stage5_CTS_t));

	ret = dev_CLA_INS_pkg(recharge_stage4->INFO, recharge_stage4->ilen, &readcardinfo_ack, sizeof(lnt_r_pub_set_readcardinfo_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}else{
			//fprintf(stderr,"lnt_init success!\n");
		}
	#endif
	
		extra->r_stat = stat;
		
		fprintf(stderr, "Off Card  Process: [R_PUB_SET_READCARDINFO] UART failed!\n\n");	
		goto Done;
	}

	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE5);
    	pkt_head.version = 0x01;
    	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage5_CTS_t) - 4);

	stage5_CTS.ilen = sizeof(lnt_r_pub_set_readcardinfo_ack_t); //信息长度
#if USE_NEW_USER_ID
		memcpy(&stage5_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(stage5_CTS.userid, 0x00, 16); //UID
#endif
	stage5_CTS.appstep = 0x01; //STEP
	memcpy(&stage5_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&stage5_CTS.INFO, &readcardinfo_ack, sizeof(stage5_CTS.INFO));  
	stage5_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage5_CTS, sizeof(lnt_packet_recharge_stage5_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage5_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &stage5_CTS, sizeof(lnt_packet_recharge_stage5_CTS_t)); //拷贝报文体

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE15, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS DD15---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE15);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE15);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE16);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS DD16---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage16_STC_t stage16_STC;
	memset(&stage16_STC, 0, sizeof(lnt_agent_off_card_stage16_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage16_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage16_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage16 Explain Error!\n");
		return agent_err;
	}

	memcpy(recharge_stage6, &(stage16_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage6_STC_t));
	
	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;
}


enum AGENT_ERR lib_lnt_off_card_stage17TO18_process(const int sockfd, agent_extra_data_t *extra, lnt_packet_recharge_stage6_STC_t *recharge_stage6, lnt_packet_recharge_stage8_STC_t *recharge_stage8)
{
	if((extra == NULL) || (recharge_stage6 == NULL) || (recharge_stage8 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_cpu_load_init_ack_t load_init_ack;
	lnt_packet_recharge_stage7_CTS_t stage7_CTS;
	
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&load_init_ack, 0, sizeof(lnt_r_cpu_load_init_ack_t));
	memset(&stage7_CTS, 0, sizeof(lnt_packet_recharge_stage7_CTS_t));

	ret = dev_CLA_INS_pkg(recharge_stage6->INFO, recharge_stage6->ilen, &load_init_ack, sizeof(lnt_r_cpu_load_init_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}else{
			//fprintf(stderr,"lnt_init success!\n");
		}
	#endif
	
		extra->r_stat = stat;
		
		fprintf(stderr, "Off Card  Process: [R_CPU_LOAD_INIT] UART failed!\n\n");	
		goto Done;
	}

	
	LIB_LNT_log_vsprintf_debug(0,"PKT SN:%d, Off Card Recharge [R_CPU_LOAD_INIT] Ok\n", lib_pkt_sn_RO_get());//add by zjc


	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE7);
	pkt_head.version = 0x01;
	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage7_CTS_t) - 4);

	stage7_CTS.ilen = sizeof(lnt_r_cpu_load_init_ack_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&stage7_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(stage7_CTS.userid, 0x00, 16); //UID
#endif
	stage7_CTS.appstep = 0x01; //STEP
	memcpy(&stage7_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&stage7_CTS.INFO, &load_init_ack, sizeof(stage7_CTS.INFO));  
	stage7_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage7_CTS, sizeof(lnt_packet_recharge_stage7_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage7_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &stage7_CTS, sizeof(lnt_packet_recharge_stage7_CTS_t)); //拷贝报文体

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE17, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS DD17---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE17);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE17);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE18);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS DD18---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage18_STC_t stage18_STC;
	memset(&stage18_STC, 0, sizeof(lnt_agent_off_card_stage18_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage18_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage18_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage18 Explain Error!\n");
		return agent_err;
	}
	
	memcpy(recharge_stage8, &(stage18_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage8_STC_t));

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;
}


enum AGENT_ERR lib_lnt_off_card_stage19TO20_process(const int sockfd, agent_extra_data_t *extra, lnt_packet_recharge_stage8_STC_t *recharge_stage8, lnt_packet_recharge_stage10_STC_t *recharge_stage10, int *r_cpu_load)
{
	if((extra == NULL) || (recharge_stage8 == NULL) || (recharge_stage10 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_cpu_load_ack_t cpu_load_ack;
	lnt_packet_recharge_stage9_CTS_t stage9_CTS;
	
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&cpu_load_ack, 0, sizeof(lnt_r_cpu_load_ack_t));
	memset(&stage9_CTS, 0, sizeof(lnt_packet_recharge_stage9_CTS_t));

	/* R_CPU_LOAD成功就是说明充值到卡已经成功 */
	ret = dev_CLA_INS_pkg(recharge_stage8->INFO, recharge_stage8->ilen, &cpu_load_ack, sizeof(lnt_r_cpu_load_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		printf("Off Card  Process: [R_CPU_LOAD] Status: %02X\n",stat);
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}
	#endif
	
		if(r_cpu_load != NULL)
			*r_cpu_load = 0xff;
		
		extra->r_stat = stat;
		
		fprintf(stderr, "Off Card  Process: [R_CPU_LOAD] UART failed!\n\n");	
		goto Done;
	}

	if(r_cpu_load != NULL)
		*r_cpu_load = 0x00;
		
	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE9);
	pkt_head.version = 0x01;
	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage9_CTS_t) - 4);

	stage9_CTS.ilen = sizeof(lnt_r_cpu_load_ack_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&stage9_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(stage9_CTS.userid, 0x00, 16); //UID
#endif
	stage9_CTS.appstep = 0x01; //STEP
	memcpy(&stage9_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&stage9_CTS.INFO, &cpu_load_ack, sizeof(stage9_CTS.INFO));  
	stage9_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage9_CTS, sizeof(lnt_packet_recharge_stage9_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage9_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &stage9_CTS, sizeof(lnt_packet_recharge_stage9_CTS_t)); //拷贝报文体

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE19, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS DD19---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE19);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE19);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE20);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS DD20---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage20_STC_t stage20_STC;
	memset(&stage20_STC, 0, sizeof(lnt_agent_off_card_stage20_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage20_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage20_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage20 Explain Error!\n");
		return agent_err;
	}

	memcpy(recharge_stage10, &(stage20_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage10_STC_t));
	
	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}


enum AGENT_ERR lib_lnt_off_card_stage21TO22_process(const int sockfd, agent_extra_data_t *extra, lnt_packet_recharge_stage10_STC_t *recharge_stage10, lnt_packet_recharge_stage12_STC_t *recharge_stage12)
{
	if((extra == NULL) || (recharge_stage10 == NULL) || (recharge_stage12 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_m1_cpu_qry_ack_t qry_ack;
	lnt_packet_recharge_stage11_CTS_t stage11_CTS;
	
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&qry_ack, 0, sizeof(lnt_r_m1_cpu_qry_ack_t));


	ret = dev_CLA_INS_pkg(recharge_stage10->INFO, recharge_stage10->ilen, &qry_ack, sizeof(lnt_r_m1_cpu_qry_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}else{
			//fprintf(stderr,"lnt_init success!\n");
		}
	#endif
	
		extra->r_stat = stat;
		
		fprintf(stderr, "Off Card  Process: [R_M1_CPU_QRY] UART failed!\n\n");	
		goto Done;
	}

	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE11);
    	pkt_head.version = 0x01;
    	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage11_CTS_t) - 4);

	stage11_CTS.ilen = sizeof(lnt_r_m1_cpu_qry_ack_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&stage11_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(stage11_CTS.userid, 0x00, 16); //UID
#endif
	stage11_CTS.appstep = 0x01; //STEP
	memcpy(&stage11_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&stage11_CTS.INFO, &qry_ack, sizeof(stage11_CTS.INFO));  
	
	stage11_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage11_CTS, sizeof(lnt_packet_recharge_stage11_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage11_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &stage11_CTS, sizeof(lnt_packet_recharge_stage11_CTS_t)); //拷贝报文体

	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE21, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS DD21---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE21);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE21);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE22);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT OFF CARD PROCESS DD22---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage22_STC_t stage22_STC;
	memset(&stage22_STC, 0, sizeof(lnt_agent_off_card_stage22_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage22_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage22_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage22 Explain Error!\n");
		return agent_err;
	}

	memcpy(recharge_stage12, &(stage22_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage12_STC_t));

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}


enum AGENT_ERR lib_lnt_off_card_stage23TO24_process(const int sockfd, agent_extra_data_t *extra, lnt_agent_off_card_stage24_STC_t *stage24, agent_retransmission_data_t *retrans)
{
	if((extra == NULL) || (stage24 == NULL) || (retrans == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	lnt_agent_off_card_stage23_CTS_t agent_stage23;

	memset(&agent_stage23, 0, sizeof(lnt_agent_off_card_stage23_CTS_t));
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&agent_stage23.pid, &extra->pid, 8); 	//PID 
	memcpy(&agent_stage23.macaddr, &g_lnt_conf.macaddr, 6);  //MAC
	memcpy(&agent_stage23.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&agent_stage23.pkt_sn, &extra->local_pkt_sn, 4); 	//PKT SN
	memcpy(&agent_stage23.indcode, &extra->indcode, 8); 	 //流程指示码
	
	memcpy(&agent_stage23.order, &extra->order, 10); 	  //订单号 add by zjc 2016-03-24
	memcpy(&agent_stage23.recharge, &extra->recharge, 4); //充值金额 add by zjc 2016-03-24

	agent_stage23.result = extra->result;		//结果
	lib_lnt_utils_time_bcd_yymmddhhMMss(agent_stage23.time);  //时间
	
	__agent_pkt_hd_set(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE23, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, &agent_stage23, sizeof(lnt_agent_off_card_stage23_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

	/* 保留重传数据 */
	retrans->cmd = LNT_HTONS(AGENT_OFF_CARD_STAGE23);
	memcpy(retrans->pid, agent_stage23.pid, 8);
	retrans->local_pkt_sn = agent_stage23.pkt_sn;  //交易流水号
	memcpy(&retrans->pkt_data, &agent_txbuf, agent_txlen);
	retrans->pkt_len = agent_txlen;

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS BB40---------------", agent_txbuf, agent_txlen , 16);
#endif

	//lib_printf_v2("--------recharge order---------",(unsigned char *)&agent_stage23.order,10,16);
	//lib_printf_v2("--------recharge amount---------",(unsigned char *)&agent_stage23.recharge,4,16);

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE23);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE23);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card Process: [%02x] READ failed!\n\n", AGENT_OFF_CARD_STAGE24);

		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_OFF_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT Off CARD PROCESS BB41---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage24;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage24_STC_t);
	agent_err = __agent_pkt_data_off_card_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Off Card stage24 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;		
}



/*----------------------------------------充值函数-------------------------------------*/
enum AGENT_ERR lib_lnt_recharge_stage1TO2_process(const int sockfd, lnt_agent_recharge_stage1_CTS_t *stage1, lnt_agent_recharge_stage2_STC_t *stage2, agent_extra_data_t *extra)
{
	if((stage1 == NULL) || (stage2 == NULL) || (extra == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	unsigned short agent_ilen = 0;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&stage1->pki, &g_lnt_conf.pki_RO, 4);  //PKI

	__agent_pkt_hd_setup(AGENT_WAY_COM, AGENT_RECHARGE_STAGE1, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_data_set(AGENT_WAY_COM, stage1, sizeof(lnt_agent_recharge_stage1_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_RECHARGE_PROC_DEBUG
		lib_printf_v2("--------------AGENT Recharge PROCESS BB05---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] WRITE failed!\n\n", AGENT_RECHARGE_STAGE1);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Recharge Process: [%02x] WRITE success\n\n", AGENT_RECHARGE_STAGE1);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] READ failed!\n\n", AGENT_RECHARGE_STAGE2);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_RECHARGE_PROC_DEBUG
	lib_printf_v2("--------------AGENT Recharge PROCESS BB06---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage2;
	agent_ptr.data_sz = sizeof(lnt_agent_recharge_stage2_STC_t);
	agent_err = __agent_pkt_data_recharge_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Recharge stage2 Explain Error!\n");
		return agent_err;
	}

	agent_ilen = LNT_NTOHS(stage2->agent_ilen);  //数据长度
	if(agent_ilen == 0)  //数据长度为0
		return AGENT_ERR_OK;

	if(stage2->agent_info) //发往读卡器的指令不为空  2016-03-07 zjc
	{
		agent_toreader_array_t toreader;
		memset(&toreader, 0, sizeof(agent_toreader_array_t));

		memcpy(&toreader, stage2->agent_info, agent_ilen);
		memcpy(extra->toreader_pki, toreader.pki, 4);
		memcpy(extra->toreader_serial, toreader.serial, 8);

		ret = __apdu_process(&(extra->str), toreader.toreader_array, &extra->r_stat); //APDU处理
		if(ret != LNT_EOK)
			goto Done;
	}
	
	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_recharge_stage3TO4_process(const int sockfd, agent_extra_data_t *extra, lnt_agent_recharge_stage4_STC_t *stage4)
{
	if((extra == NULL) || (stage4 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[1024] = {0};
	unsigned char agent_rxbuf[1204] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned short agent_ilen =  LNT_HTONS(extra->str.s_len + 13);
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	

	__agent_pkt_hd_setup(AGENT_WAY_COM, AGENT_RECHARGE_STAGE3, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, g_lnt_conf.pki_RO, 4);  //PKI

	__agent_pkt_data_set(AGENT_WAY_COM, &agent_ilen, 2); //信息长度
	__agent_pkt_data_set(AGENT_WAY_COM, extra->toreader_pki, 4);  //toreader PKI
	__agent_pkt_data_set(AGENT_WAY_COM, extra->toreader_serial, 8); //toreader serial
	__agent_pkt_char_set(AGENT_WAY_COM, 0x01); //step
	__agent_pkt_data_set(AGENT_WAY_COM, extra->str.s_str, extra->str.s_len);  //string

	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_RECHARGE_PROC_DEBUG
		lib_printf_v2("--------------AGENT Recharge PROCESS BB34---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] WRITE failed!\n\n", AGENT_RECHARGE_STAGE3);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Recharge Process: [%02x] WRITE success\n\n", AGENT_RECHARGE_STAGE3);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] READ failed!\n\n", AGENT_RECHARGE_STAGE4);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_RECHARGE_PROC_DEBUG
	lib_printf_v2("--------------AGENT Recharge PROCESS BB35---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage4;
	agent_ptr.data_sz = sizeof(lnt_agent_recharge_stage4_STC_t);
	agent_err = __agent_pkt_data_recharge_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Recharge stage4 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;		
}

enum AGENT_ERR lib_lnt_recharge_stage5TO6_process(const int sockfd, agent_extra_data_t *extra, recharge_info_t *recharge,  lnt_packet_recharge_stage2_STC_t *recharge_stage2)
{
	if((extra == NULL) || (recharge == NULL) || (recharge_stage2 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	lnt_packet_recharge_stage1_CTS_t recharge_stage1_CTS;
		
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&recharge_stage1_CTS, 0, sizeof(lnt_packet_recharge_stage1_CTS_t));


	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE1);
	pkt_head.version = 0x01;
	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage1_CTS_t) - 4);

	recharge_stage1_CTS.ilen = sizeof(recharge_info_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&recharge_stage1_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(&recharge_stage1_CTS.userid, 0x00, 16); //UID
#endif
	recharge_stage1_CTS.appstep = 0x00; //STEP
	memcpy(&recharge_stage1_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&recharge_stage1_CTS.INFO, recharge, sizeof(recharge_stage1_CTS.INFO));  
	recharge_stage1_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&recharge_stage1_CTS, sizeof(lnt_packet_recharge_stage1_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage1_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &recharge_stage1_CTS, sizeof(lnt_packet_recharge_stage1_CTS_t)); //拷贝报文体

	__agent_pkt_hd_setup(AGENT_WAY_COM, AGENT_RECHARGE_STAGE5, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_RECHARGE_PROC_DEBUG
		lib_printf_v2("--------------AGENT Recharge PROCESS DD11---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] WRITE failed!\n\n", AGENT_RECHARGE_STAGE5);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Recharge Process: [%02x] WRITE success\n\n", AGENT_RECHARGE_STAGE5);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] READ failed!\n\n", AGENT_RECHARGE_STAGE6);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_RECHARGE_PROC_DEBUG
	lib_printf_v2("--------------AGENT RECHARGE PROCESS DD12---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage12_STC_t stage12_STC;
	memset(&stage12_STC, 0, sizeof(lnt_agent_off_card_stage12_STC_t));

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage12_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage12_STC_t);
	agent_err = __agent_pkt_data_recharge_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Recharge stage6 Explain Error!\n");
		
		#if 1
		if(agent_err == AGENT_ERR_CINFO_ERROR_NOT_REGISTER)	
		{
			fprintf(stderr, "Recharge Process: [DD12] CARD NOT REGISTER failed!\n\n");
				
			__lnt_register_notify_put(LNT_REGISTER_STAT_RE);  //通知签到线程签到

			lib_disconnect(sockfd);
		}
		#endif
		
		return agent_err;
	}

	memcpy(recharge_stage2, &(stage12_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage2_STC_t));

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_recharge_stage7TO8_process(const int sockfd, agent_extra_data_t *extra,  lnt_packet_recharge_stage2_STC_t *recharge_stage2, lnt_packet_recharge_stage4_STC_t *recharge_stage4)
{
	if((extra == NULL) || (recharge_stage2 == NULL) || (recharge_stage4 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_pub_qry_physics_info_Encry_ack_t phy_info_encry;//加密
	lnt_packet_recharge_stage3_CTS_t recharge_stage3;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&phy_info_encry, 0, sizeof(lnt_r_pub_qry_physics_info_Encry_ack_t));
	memset(&recharge_stage3, 0, sizeof(lnt_packet_recharge_stage3_CTS_t));

	ret = dev_CLA_INS_pkg(recharge_stage2->INFO, recharge_stage2->ilen, &phy_info_encry, sizeof(lnt_r_pub_qry_physics_info_Encry_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}else{
			//fprintf(stderr,"lnt_init success!\n");
		}
	#endif
	
		extra->r_stat = stat;
		
		fprintf(stderr, "RECHARGE  Process: [R_PUB_QRY_PHYSICS_INFO_ENCRY] UART failed!\n\n");	
		goto Done;
	}

	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE3);
	pkt_head.version = 0x01;
	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage3_CTS_t) - 4);

	recharge_stage3.ilen = sizeof(lnt_r_pub_qry_physics_info_Encry_ack_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&recharge_stage3.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(&recharge_stage3.userid, 0x00, 16); //UID
#endif
	recharge_stage3.appstep = 0x01; //STEP
	memcpy(&recharge_stage3.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&recharge_stage3.INFO.Encry_INFO, &phy_info_encry, sizeof(recharge_stage3.INFO.Encry_INFO));  
	recharge_stage3.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&recharge_stage3, sizeof(lnt_packet_recharge_stage3_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage3_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &recharge_stage3, sizeof(lnt_packet_recharge_stage3_CTS_t)); //拷贝报文体

	__agent_pkt_hd_setup(AGENT_WAY_COM, AGENT_RECHARGE_STAGE7, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_RECHARGE_PROC_DEBUG
		lib_printf_v2("--------------AGENT RECHARGE PROCESS DD13---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] WRITE failed!\n\n", AGENT_RECHARGE_STAGE7);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Recharge Process: [%02x] WRITE success\n\n", AGENT_RECHARGE_STAGE7);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] READ failed!\n\n", AGENT_RECHARGE_STAGE8);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_RECHARGE_PROC_DEBUG
	lib_printf_v2("--------------AGENT RECHARGE PROCESS DD14---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage14_STC_t stage14_STC;
	memset(&stage14_STC, 0, sizeof(lnt_agent_off_card_stage14_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage14_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage14_STC_t);
	agent_err = __agent_pkt_data_recharge_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		if(agent_err == 0xBB)  //add by zjc 2016-03-21
		{
			fprintf(stderr, "Recharge Process: [DD14] CARD NOT REGISTER failed!\n\n");
				
			__lnt_register_notify_put(LNT_REGISTER_STAT_RE);  //通知签到线程签到

			lib_disconnect(sockfd);
			return agent_err;
		}
		
		fprintf(stderr, "Recharge stage8 Explain Error!\n");
		return agent_err;
	}

	memcpy(recharge_stage4, &(stage14_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage4_STC_t));
	
	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;
}

enum AGENT_ERR lib_lnt_recharge_stage9TO10_process(const int sockfd, agent_extra_data_t *extra, lnt_packet_recharge_stage4_STC_t *recharge_stage4, lnt_packet_recharge_stage6_STC_t *recharge_stage6)
{
	if((extra == NULL) || (recharge_stage4 == NULL) || (recharge_stage6 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_pub_set_readcardinfo_ack_t readcardinfo_ack;
	lnt_packet_recharge_stage5_CTS_t stage5_CTS;
	
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&readcardinfo_ack, 0, sizeof(lnt_r_pub_set_readcardinfo_ack_t));
	memset(&stage5_CTS, 0, sizeof(lnt_packet_recharge_stage5_CTS_t));

	ret = dev_CLA_INS_pkg(recharge_stage4->INFO, recharge_stage4->ilen, &readcardinfo_ack, sizeof(lnt_r_pub_set_readcardinfo_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "Recharge Process: [R_PUB_SET_READCARDINFO] Stat:%02X\n",stat); 
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}else{
			//fprintf(stderr,"lnt_init success!\n");
		}
	#endif
	
		extra->r_stat = stat;
		
		fprintf(stderr, "Recharge  Process: [R_PUB_SET_READCARDINFO] UART failed!\n\n");	
		goto Done;
	}

	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE5);
    	pkt_head.version = 0x01;
    	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage5_CTS_t) - 4);

	stage5_CTS.ilen = sizeof(lnt_r_pub_set_readcardinfo_ack_t); //信息长度
#if USE_NEW_USER_ID
		memcpy(&stage5_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(stage5_CTS.userid, 0x00, 16); //UID
#endif
	stage5_CTS.appstep = 0x01; //STEP
	memcpy(&stage5_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&stage5_CTS.INFO, &readcardinfo_ack, sizeof(stage5_CTS.INFO));  
	stage5_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage5_CTS, sizeof(lnt_packet_recharge_stage5_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage5_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &stage5_CTS, sizeof(lnt_packet_recharge_stage5_CTS_t)); //拷贝报文体

	__agent_pkt_hd_setup(AGENT_WAY_COM, AGENT_RECHARGE_STAGE9, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_RECHARGE_PROC_DEBUG
		lib_printf_v2("--------------AGENT RECHARGE PROCESS DD15---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] WRITE failed!\n\n", AGENT_RECHARGE_STAGE9);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Recharge Process: [%02x] WRITE success\n\n", AGENT_RECHARGE_STAGE9);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] READ failed!\n\n", AGENT_RECHARGE_STAGE10);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_RECHARGE_PROC_DEBUG
	lib_printf_v2("--------------AGENT RECHARGE PROCESS DD16---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage16_STC_t stage16_STC;
	memset(&stage16_STC, 0, sizeof(lnt_agent_off_card_stage16_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage16_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage16_STC_t);
	agent_err = __agent_pkt_data_recharge_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Recharge stage10 Explain Error!\n");
		return agent_err;
	}

	memcpy(recharge_stage6, &(stage16_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage6_STC_t));
	
	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;
}


enum AGENT_ERR lib_lnt_recharge_stage11TO12_process(const int sockfd, agent_extra_data_t *extra, lnt_packet_recharge_stage6_STC_t *recharge_stage6, lnt_packet_recharge_stage8_STC_t *recharge_stage8)
{
	if((extra == NULL) || (recharge_stage6 == NULL) || (recharge_stage8 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_cpu_load_init_ack_t load_init_ack;
	lnt_packet_recharge_stage7_CTS_t stage7_CTS;
	
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&load_init_ack, 0, sizeof(lnt_r_cpu_load_init_ack_t));
	memset(&stage7_CTS, 0, sizeof(lnt_packet_recharge_stage7_CTS_t));

	ret = dev_CLA_INS_pkg(recharge_stage6->INFO, recharge_stage6->ilen, &load_init_ack, sizeof(lnt_r_cpu_load_init_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "Recharge Process: [R_CPU_LOAD_INIT] Stat:%02X\n",stat);	
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}else{
			//fprintf(stderr,"lnt_init success!\n");
		}
	#endif
	
		extra->r_stat = stat;
		
		fprintf(stderr, "Recharge  Process: [R_CPU_LOAD_INIT] UART failed!\n\n");	
		goto Done;
	}

	
	LIB_LNT_log_vsprintf_debug(0,"PKT SN:%d, Recharge [R_CPU_LOAD_INIT] Ok\n", lib_pkt_sn_RO_get());//add by zjc


	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE7);
	pkt_head.version = 0x01;
	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage7_CTS_t) - 4);

	stage7_CTS.ilen = sizeof(lnt_r_cpu_load_init_ack_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&stage7_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(stage7_CTS.userid, 0x00, 16); //UID
#endif
	stage7_CTS.appstep = 0x01; //STEP
	memcpy(&stage7_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&stage7_CTS.INFO, &load_init_ack, sizeof(stage7_CTS.INFO));  
	stage7_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage7_CTS, sizeof(lnt_packet_recharge_stage7_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage7_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &stage7_CTS, sizeof(lnt_packet_recharge_stage7_CTS_t)); //拷贝报文体

	__agent_pkt_hd_setup(AGENT_WAY_COM, AGENT_RECHARGE_STAGE11, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_RECHARGE_PROC_DEBUG
		lib_printf_v2("--------------AGENT RECHARGE PROCESS DD17---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] WRITE failed!\n\n", AGENT_RECHARGE_STAGE11);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "RECHARGE Process: [%02x] WRITE success\n\n", AGENT_RECHARGE_STAGE11);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] READ failed!\n\n", AGENT_RECHARGE_STAGE12);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_RECHARGE_PROC_DEBUG
	lib_printf_v2("--------------AGENT RECHARGE PROCESS DD18---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage18_STC_t stage18_STC;
	memset(&stage18_STC, 0, sizeof(lnt_agent_off_card_stage18_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage18_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage18_STC_t);
	agent_err = __agent_pkt_data_recharge_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Recharge stage12 Explain Error!\n");
		return agent_err;
	}
	
	memcpy(recharge_stage8, &(stage18_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage8_STC_t));

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;
}


enum AGENT_ERR lib_lnt_recharge_stage13TO14_process(const int sockfd, agent_extra_data_t *extra, lnt_packet_recharge_stage8_STC_t *recharge_stage8, lnt_packet_recharge_stage10_STC_t *recharge_stage10, int *r_cpu_load)
{
	if((extra == NULL) || (recharge_stage8 == NULL) || (recharge_stage10 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_cpu_load_ack_t cpu_load_ack;
	lnt_packet_recharge_stage9_CTS_t stage9_CTS;
	
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&cpu_load_ack, 0, sizeof(lnt_r_cpu_load_ack_t));
	memset(&stage9_CTS, 0, sizeof(lnt_packet_recharge_stage9_CTS_t));

	/* R_CPU_LOAD成功就是说明充值到卡已经成功 */
	ret = dev_CLA_INS_pkg(recharge_stage8->INFO, recharge_stage8->ilen, &cpu_load_ack, sizeof(lnt_r_cpu_load_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "Recharge Process: [R_CPU_LOAD] Stat:%02X\n",stat);	
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}
	#endif
	
		if(r_cpu_load != NULL)
			*r_cpu_load = 0xff;
		
		extra->r_stat = stat;
		
		fprintf(stderr, "Recharge  Process: [R_CPU_LOAD] UART failed!\n\n");	
		goto Done;
	}

	if(r_cpu_load != NULL)
		*r_cpu_load = 0x00;
		
	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE9);
	pkt_head.version = 0x01;
	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage9_CTS_t) - 4);

	stage9_CTS.ilen = sizeof(lnt_r_cpu_load_ack_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&stage9_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(stage9_CTS.userid, 0x00, 16); //UID
#endif
	stage9_CTS.appstep = 0x01; //STEP
	memcpy(&stage9_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&stage9_CTS.INFO, &cpu_load_ack, sizeof(stage9_CTS.INFO));  
	stage9_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage9_CTS, sizeof(lnt_packet_recharge_stage9_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage9_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &stage9_CTS, sizeof(lnt_packet_recharge_stage9_CTS_t)); //拷贝报文体

	__agent_pkt_hd_setup(AGENT_WAY_COM, AGENT_RECHARGE_STAGE13, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_RECHARGE_PROC_DEBUG
		lib_printf_v2("--------------AGENT RECHARGE PROCESS DD19---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] WRITE failed!\n\n", AGENT_RECHARGE_STAGE13);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Recharge Process: [%02x] WRITE success\n\n", AGENT_RECHARGE_STAGE13);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] READ failed!\n\n", AGENT_RECHARGE_STAGE14);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_RECHARGE_PROC_DEBUG
	lib_printf_v2("--------------AGENT RECHARGE PROCESS DD20---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage20_STC_t stage20_STC;
	memset(&stage20_STC, 0, sizeof(lnt_agent_off_card_stage20_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage20_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage20_STC_t);
	agent_err = __agent_pkt_data_recharge_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Recharge stage14 Explain Error!\n");
		return agent_err;
	}

	memcpy(recharge_stage10, &(stage20_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage10_STC_t));
	
	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}


enum AGENT_ERR lib_lnt_recharge_stage15TO16_process(const int sockfd, agent_extra_data_t *extra, lnt_packet_recharge_stage10_STC_t *recharge_stage10, lnt_packet_recharge_stage12_STC_t *recharge_stage12)
{
	if((extra == NULL) || (recharge_stage10 == NULL) || (recharge_stage12 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lnt_r_m1_cpu_qry_ack_t qry_ack;
	lnt_packet_recharge_stage11_CTS_t stage11_CTS;
	
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&qry_ack, 0, sizeof(lnt_r_m1_cpu_qry_ack_t));


	ret = dev_CLA_INS_pkg(recharge_stage10->INFO, recharge_stage10->ilen, &qry_ack, sizeof(lnt_r_m1_cpu_qry_ack_t), &stat, 500);
	if((ret <=  0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "Recharge Process: [R_M1_CPU_QRY] Stat:%02X\n",stat);	
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}else{
			//fprintf(stderr,"lnt_init success!\n");
		}
	#endif
	
		extra->r_stat = stat;
		
		fprintf(stderr, "Recharge  Process: [R_M1_CPU_QRY] UART failed!\n\n");	
		goto Done;
	}

	pkt_head.head = LNT_HTONS(LNT_RECHARGE_STAGE11);
    	pkt_head.version = 0x01;
    	pkt_head.length = LNT_HTONS(sizeof(lnt_packet_recharge_stage11_CTS_t) - 4);

	stage11_CTS.ilen = sizeof(lnt_r_m1_cpu_qry_ack_t); //信息长度
#if USE_NEW_USER_ID
	memcpy(&stage11_CTS.userid[16 - UID_LEN],&USER_ID,UID_LEN);
#else
	memset(stage11_CTS.userid, 0x00, 16); //UID
#endif
	stage11_CTS.appstep = 0x01; //STEP
	memcpy(&stage11_CTS.pki, &g_lnt_conf.pki_RO, 4);  //PKI 
	memcpy(&stage11_CTS.INFO, &qry_ack, sizeof(stage11_CTS.INFO));  
	
	stage11_CTS.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&stage11_CTS, sizeof(lnt_packet_recharge_stage11_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_packet_recharge_stage11_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &stage11_CTS, sizeof(lnt_packet_recharge_stage11_CTS_t)); //拷贝报文体

	__agent_pkt_hd_setup(AGENT_WAY_COM, AGENT_RECHARGE_STAGE15, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_RECHARGE_PROC_DEBUG
		lib_printf_v2("--------------AGENT RECHARGE PROCESS DD21---------------", agent_txbuf, agent_txlen , 16);
#endif


	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] WRITE failed!\n\n", AGENT_RECHARGE_STAGE15);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Recharge Process: [%02x] WRITE success\n\n", AGENT_RECHARGE_STAGE15);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] READ failed!\n\n", AGENT_RECHARGE_STAGE16);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_RECHARGE_PROC_DEBUG
	lib_printf_v2("--------------AGENT RECHARGE PROCESS DD22---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	lnt_agent_off_card_stage22_STC_t stage22_STC;
	memset(&stage22_STC, 0, sizeof(lnt_agent_off_card_stage22_STC_t));
	
	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&stage22_STC;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage22_STC_t);
	agent_err = __agent_pkt_data_recharge_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Recharge stage16 Explain Error!\n");
		return agent_err;
	}

	memcpy(recharge_stage12, &(stage22_STC.agent_info[sizeof(lnt_packet_head_t)]), sizeof(lnt_packet_recharge_stage12_STC_t));

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}


enum AGENT_ERR lib_lnt_recharge_stage17TO18_process(const int sockfd, agent_extra_data_t *extra, lnt_agent_off_card_stage24_STC_t *stage24, agent_retransmission_data_t *retrans)
{
	if((extra == NULL) || (stage24 == NULL) || (retrans == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	lnt_agent_off_card_stage23_CTS_t agent_stage23;

	memset(&agent_stage23, 0, sizeof(lnt_agent_off_card_stage23_CTS_t));
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&agent_stage23.pid, &extra->pid, 8); 	//PID 
	memcpy(&agent_stage23.macaddr, &g_lnt_conf.macaddr, 6);  //MAC
	memcpy(&agent_stage23.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&agent_stage23.pkt_sn, &extra->local_pkt_sn, 4); 	//PKT SN
	memcpy(&agent_stage23.indcode, &extra->indcode, 8); 	 //流程指示码
	
	memcpy(&agent_stage23.order, &extra->order, 10); 	  //订单号 add by zjc 2016-03-24
	memcpy(&agent_stage23.recharge, &extra->recharge, 4); //充值金额 add by zjc 2016-03-24

	agent_stage23.result = extra->result;		//结果
	lib_lnt_utils_time_bcd_yymmddhhMMss(agent_stage23.time);  //时间
	
	__agent_pkt_hd_setup(AGENT_WAY_COM, AGENT_RECHARGE_STAGE17, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, &agent_stage23, sizeof(lnt_agent_off_card_stage23_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

	/* 保留重传数据 */
	retrans->cmd = LNT_HTONS(AGENT_RECHARGE_STAGE17);
	memcpy(retrans->pid, agent_stage23.pid, 8);
	retrans->local_pkt_sn = agent_stage23.pkt_sn;  //交易流水号
	memcpy(&retrans->pkt_data, &agent_txbuf, agent_txlen);
	retrans->pkt_len = agent_txlen;

#ifdef LNT_RECHARGE_PROC_DEBUG
		lib_printf_v2("--------------AGENT RECHARGE PROCESS BB40---------------", agent_txbuf, agent_txlen , 16);
#endif

	//lib_printf_v2("--------recharge order---------",(unsigned char *)&agent_stage23.order,10,16);
	//lib_printf_v2("--------recharge amount---------",(unsigned char *)&agent_stage23.recharge,4,16);

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] WRITE failed!\n\n", AGENT_RECHARGE_STAGE17);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Recharge Process: [%02x] WRITE success\n\n", AGENT_RECHARGE_STAGE17);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "Recharge Process: [%02x] READ failed!\n\n", AGENT_RECHARGE_STAGE18);

		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_RECHARGE_PROC_DEBUG
	lib_printf_v2("--------------AGENT RECHARGE PROCESS BB41---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage24;
	agent_ptr.data_sz = sizeof(lnt_agent_off_card_stage24_STC_t);
	agent_err = __agent_pkt_data_recharge_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "Recharge stage18 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;		
}

/*开卡函数（第三方扣费）*/
enum AGENT_ERR lib_lnt_on_card_otherConsume_stage1TO2_process(const int sockfd, lnt_agent_on_card_otherConsume_stage1_CTS_t *stage1, lnt_agent_on_card_stage2_STC_t *stage2)
{
	if((stage1 == NULL) || (stage1 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&stage1->pki, &g_lnt_conf.pki_RO, 4);  //PKI

	__agent_pkt_hd_fill(AGENT_WAY_COM, AGENT_ON_CARD_OTHER_CONSUME_STAGE1, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_data_set(AGENT_WAY_COM, stage1, sizeof(lnt_agent_on_card_otherConsume_stage1_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_ON_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT ON CARD OTHER_COMSUME PROCESS BB07---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card OtherConsume Process: [%02x] WRITE failed!\n\n", AGENT_ON_CARD_OTHER_CONSUME_STAGE1);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "On Card OtherConsume Process: [%02x] WRITE success\n\n", AGENT_ON_CARD_OTHER_CONSUME_STAGE1);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card OtherConsume Process: [%02x] READ failed!\n\n", AGENT_ON_CARD_OTHER_CONSUME_STAGE2);
		
		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_ON_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT ON CARD OTHER_COMSUME PROCESS BB08---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage2;
	agent_ptr.data_sz = sizeof(lnt_agent_on_card_stage2_STC_t);
	agent_err = __agent_pkt_data_on_card_otherConsume_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "On Card OtherConsume stage2 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;
}
enum AGENT_ERR lib_lnt_on_card_otherConsume_stage3TO4_process(const int sockfd, agent_extra_data_t *extra, lnt_member_process_stage2_STC_t *member_stage2, unsigned char *stat)
{
	if((extra == NULL) || (member_stage2 == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	unsigned char txbuf[512] = {0};
	unsigned int txlen = 0;
	unsigned int crc32 = 0;
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	unsigned char mstat = LNT_MOC_ERROR_UNKNOW;
	lnt_r_deposit_init_req_t dpinit_req;
	lnt_r_deposit_init_ack_t dpinit_ack;
	unsigned int pkt_head_len = sizeof(lnt_packet_head_t);
	lnt_packet_head_t pkt_head;
	lnt_agent_on_card_stage6_STC_t agent_stage6;
	lnt_member_process_stage1_CTS_t member_stage1;

	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memset(&dpinit_req, 0, sizeof(lnt_r_deposit_init_req_t));
	memset(&dpinit_ack, 0, sizeof(lnt_r_deposit_init_ack_t));
	memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
	memset(&agent_stage6, 0, sizeof(lnt_agent_on_card_stage6_STC_t));
	memset(&member_stage1, 0, sizeof(lnt_member_process_stage1_CTS_t));
	
	dpinit_req.area = 0x01; //广州
	dpinit_req.dtype = 0x00;  //普通用户
	dpinit_req.member_op = LNT_MEMBER_OP_REGISTER;  //开卡
	dpinit_req.spno = 0x0101; //一定要0x0101,不然DD74的返回信息码是0xEE
	dpinit_req.member_mark = 0x00;  
	
	ret = dev_r_deposit_init(&dpinit_req, &dpinit_ack, &mstat, 500); //押金初始化
	if((ret <=  0) || (mstat != LNT_MOC_ERROR_NORMAL))
	{
	#if 1 //add by zjc
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty,"/dev/ttyO1");
		config.baudrate = 115200;
	
		ret = lnt_init(&config);
		if(ret < 0){
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		}
	#endif
	
		if(stat != NULL)
			*stat = mstat;
		
		fprintf(stderr, "On Card OtherConsume Process: [deposit init] UART failed!\n\n");	
		goto Done;
	}

#if USE_NEW_USER_ID
	memcpy(&member_stage1.userid[16 - UID_LEN],USER_ID,UID_LEN);
#else
	memcpy(member_stage1.userid, g_lnt_conf.userid, 16);  //用户ID
#endif
	//lib_printf("member_stage1.userid",&member_stage1.userid,16);

	member_stage1.appstep = 0x00;  //应用步骤
	memcpy(&member_stage1.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&member_stage1.INFO, &dpinit_ack.data, sizeof(dpinit_ack.data));

	pkt_head.head = LNT_HTONS(LNT_MEMBER_REGISTER);
	pkt_head.version = 0x01;
	pkt_head.length = LNT_HTONS(sizeof(lnt_member_process_stage1_CTS_t) - 4);

	member_stage1.ilen = sizeof(dpinit_ack.data);
	member_stage1.crc32 = LNT_HTONL(lnt_crc32((unsigned char *)&member_stage1, sizeof(lnt_member_process_stage1_CTS_t) - 4)); //计算校验
	txlen = pkt_head_len + sizeof(lnt_member_process_stage1_CTS_t);
	memcpy(txbuf, &pkt_head, pkt_head_len);  //拷贝报文头
	memcpy(&(txbuf[pkt_head_len]), &member_stage1, sizeof(lnt_member_process_stage1_CTS_t)); //拷贝报文体
	
	__agent_pkt_hd_fill(AGENT_WAY_COM, AGENT_ON_CARD_OTHER_CONSUME_STAGE3, AGENT_ATTR_PASS);  //透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, extra->pid, 8);  //PID
	__agent_pkt_data_set(AGENT_WAY_COM, extra->macaddr, 6);  //MAC
	__agent_pkt_data_set(AGENT_WAY_COM, &g_lnt_conf.pki_RO, 4);  //pki
	__agent_pkt_data_set(AGENT_WAY_COM, &txlen, 1);  //数据长度
	__agent_pkt_data_set(AGENT_WAY_COM, txbuf, txlen);     //数据
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_ON_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT ON CARD OTHER_COMSUME PROCESS DD73---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card OtherConsume Process: [%02x] WRITE failed!\n\n", AGENT_ON_CARD_OTHER_CONSUME_STAGE3);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "On Card OtherConsume Process: [%02x] WRITE success\n\n", AGENT_ON_CARD_OTHER_CONSUME_STAGE3);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card OtherConsume Process: [%02x] READ failed!\n\n", AGENT_ON_CARD_OTHER_CONSUME_STAGE4);

		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_ON_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT ON CARD OTHER_COMSUME PROCESS DD74---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)&agent_stage6;
	agent_ptr.data_sz = sizeof(lnt_agent_on_card_stage6_STC_t);
	agent_err = __agent_pkt_data_on_card_otherConsume_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "On Card OtherConsume stage4 Explain Error!\n");
		return agent_err;
	}

	memset(&pkt_head, 0, sizeof(lnt_packet_head_t));
	memcpy(&pkt_head, agent_stage6.agent_info, pkt_head_len);
   	memcpy(member_stage2, &agent_stage6.agent_info[pkt_head_len], sizeof(lnt_member_process_stage2_STC_t));

#if 0
	if(member_stage2->retcode != 0x00) //返回信息码
	{
		fprintf(stderr, "On Card Process: [DD74] RETURN CODE failed!\n\n");
		
		lib_disconnect(sockfd);
		goto Done;
	}
#endif
	printf("On Card OtherConsume Process: [DD74] retcode: %02X\n",member_stage2->retcode);
	if(member_stage2->retcode != 0x00)  
	{
		fprintf(stderr, "On Card OtherConsume Process: [DD74] CARD NOT REGISTER failed!\n\n");
			
		__lnt_register_notify_put(LNT_REGISTER_STAT_RE);  //通知签到线程签到

		lib_disconnect(sockfd);
		return member_stage2->retcode;
	}

	crc32 = lnt_crc32((unsigned char *)member_stage2, sizeof(lnt_member_process_stage2_STC_t) - 4); //计算校验
	if(LNT_NTOHL(member_stage2->crc32) != crc32)  //检验码
	{
		fprintf(stderr, "On Card OtherConsume Process: [DD74] CRC failed!\n\n");
		
		lib_disconnect(sockfd);
		return AGENT_ERR_CRC;
	}	

	#if 0
	if(member_stage2->indcode != 0xFF) //流程指示码
	{
		fprintf(stderr, "On Card Process: [DD74] STEP failed!\n\n");

		lib_disconnect(sockfd);
		goto Done;
	}
	#endif
	
	fprintf(stderr, "On Card OtherConsume Process: [DD74] READ success\n\n");

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_on_card_otherConsume_stage5TO6_process(const int sockfd, agent_extra_data_t *extra, lnt_agent_on_card_stage8_STC_t *stage8, agent_retransmission_data_t *retrans)
{
	if((extra == NULL) || (stage8 == NULL) || (retrans == NULL))
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	unsigned char agent_rxbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	enum AGENT_ERR agent_err = AGENT_ERR_UNKNOWN;
	lnt_agent_on_card_otherConsume_stage5_CTS_t agent_stage5;

	memset(&agent_stage5, 0, sizeof(lnt_agent_on_card_otherConsume_stage5_CTS_t));
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&agent_stage5.pid, &extra->pid, 8); 	//PID 
	memcpy(&agent_stage5.macaddr, &g_lnt_conf.macaddr, 6);  //MAC
	memcpy(&agent_stage5.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&agent_stage5.pkt_sn, &extra->local_pkt_sn, 4); 	//PKT SN
	memcpy(&agent_stage5.indcode, &extra->indcode, 8); 	 //流程指示码
	agent_stage5.result = extra->result;		//结果
	memcpy(&agent_stage5.consume_number, &extra->consume_number, 10);
	lib_lnt_utils_time_bcd_yymmddhhMMss(agent_stage5.time);  //时间
	
	__agent_pkt_hd_fill(AGENT_WAY_COM, AGENT_ON_CARD_OTHER_CONSUME_STAGE5, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, &agent_stage5, sizeof(lnt_agent_on_card_otherConsume_stage5_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

	/* 保留重传数据 */
	retrans->cmd = LNT_HTONS(AGENT_ON_CARD_OTHER_CONSUME_STAGE5);
	memcpy(retrans->pid, agent_stage5.pid, 8);
	retrans->local_pkt_sn = g_lnt_conf.pkt_sn_RO;  //交易流水号->报文序列号
	memcpy(&retrans->pkt_data, &agent_txbuf, agent_txlen);
	retrans->pkt_len = agent_txlen;

//goto Done; //test for retrans

#ifdef LNT_ON_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT ON CARD OTHER_COMSUME PROCESS BB22---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card OtherConsume Process: [%02x] WRITE failed!\n\n", AGENT_ON_CARD_OTHER_CONSUME_STAGE5);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "On Card OtherConsume Process: [%02x] WRITE success\n\n", AGENT_ON_CARD_OTHER_CONSUME_STAGE5);

	ret = lib_tcp_read_select(sockfd, agent_rxbuf, sizeof(agent_rxbuf), g_lnt_conf.delay_ms);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card OtherConsume Process: [%02x] READ failed!\n\n", AGENT_ON_CARD_OTHER_CONSUME_STAGE6);

		lib_disconnect(sockfd);
		return AGENT_ERR_NOT_DATA;
	}

#ifdef LNT_ON_CARD_PROC_DEBUG
	lib_printf_v2("--------------AGENT ON CARD OTHER_COMSUME PROCESS BB23---------------", agent_rxbuf, ret, 16);
	fprintf(stderr, "--------------------------END---------------------\n");
#endif

	agent_ptr.exp_ptr = (char *)&agent_rxbuf;
	agent_ptr.const_exp_sz = ret;
	agent_ptr.extra_ptr = NULL;
	agent_ptr.extra_sz = 0;
	agent_ptr.data_ptr = (char *)stage8;
	agent_ptr.data_sz = sizeof(lnt_agent_on_card_stage8_STC_t);
	agent_err = __agent_pkt_data_on_card_otherConsume_explain(&agent_ptr);
	if(agent_err != AGENT_ERR_OK)
	{
		fprintf(stderr, "On Card OtherConsume stage6 Explain Error!\n");
		return agent_err;
	}

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}


//-------------------------------------------------------------------------------------//


enum AGENT_ERR lib_lnt_on_card_blacklist_process(const int sockfd, agent_extra_data_t *extra)
{
	if(extra == NULL)
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	lnt_agent_on_card_stage7_CTS_t agent_stage7;

	memset(&agent_stage7, 0, sizeof(lnt_agent_on_card_stage7_CTS_t));
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&agent_stage7.pid, &extra->pid, 8); 	//物理卡号
	memcpy(&agent_stage7.macaddr, &g_lnt_conf.macaddr, 6);  //MAC
	memcpy(&agent_stage7.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&agent_stage7.pkt_sn, &extra->local_pkt_sn, 4); 	//PKT SN
	memcpy(&agent_stage7.indcode, &extra->lid, 8); 	 //逻辑卡号
	agent_stage7.result = 0x01;		//黑名单卡
	lib_lnt_utils_time_bcd_yymmddhhMMss(agent_stage7.time);  //时间

	__agent_pkt_hd_fill(AGENT_WAY_COM, AGENT_ON_CARD_STAGE7, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, &agent_stage7, sizeof(lnt_agent_on_card_stage7_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_ON_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT ON CARD PROCESS BLACKLIST---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "On Card BlackList Process: [%02x] WRITE failed!\n\n", AGENT_ON_CARD_STAGE7);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "On Card BlackList Process: [%02x] WRITE success\n\n", AGENT_ON_CARD_STAGE7);
	
	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}

enum AGENT_ERR lib_lnt_off_card_blacklist_process(const int sockfd, agent_extra_data_t *extra)
{
	if(extra == NULL)
		goto Done;

	int ret = LNT_ERROR;
	int agent_txlen = 0;
	unsigned char agent_txbuf[512] = {0};
	struct agent_pkt_ptr agent_ptr;
	lnt_agent_off_card_stage9_CTS_t agent_stage9;

	memset(&agent_stage9, 0, sizeof(lnt_agent_off_card_stage9_CTS_t));
	memset(&agent_ptr, 0, sizeof(struct agent_pkt_ptr));
	memcpy(&agent_stage9.pid, &extra->pid, 8); 	//PID 
	memcpy(&agent_stage9.macaddr, &g_lnt_conf.macaddr, 6);  //MAC
	memcpy(&agent_stage9.pki, &g_lnt_conf.pki_RO, 4);  //PKI
	memcpy(&agent_stage9.pkt_sn, &extra->local_pkt_sn, 4); 	//PKT SN
	memcpy(&agent_stage9.indcode, &extra->lid, 8); 	 //LID
	agent_stage9.result = 0x01;		//结果
	lib_lnt_utils_time_bcd_yymmddhhMMss(agent_stage9.time);  //时间
	
	__agent_pkt_hd_fill(AGENT_WAY_COM, AGENT_OFF_CARD_STAGE9, AGENT_ATTR_NOT_PASS);  //不透传
	__agent_pkt_set_sn(AGENT_WAY_COM, g_lnt_conf.pkt_sn_RO); //报文序列号
	__agent_pkt_data_set(AGENT_WAY_COM, &agent_stage9, sizeof(lnt_agent_off_card_stage9_CTS_t));  
	__agent_pkt_crc32_set(AGENT_WAY_COM);
	agent_txlen = __agent_pkt_data_get(AGENT_WAY_COM, agent_txbuf);

#ifdef LNT_OFF_CARD_PROC_DEBUG
		lib_printf_v2("--------------AGENT OFF CARD PROCESS BLACKLIST---------------", agent_txbuf, agent_txlen , 16);
#endif

	ret = lib_tcp_writen(sockfd, agent_txbuf, agent_txlen);
	if(ret <= 0)
	{
		fprintf(stderr, "Off Card BLACKLIST Process: [%02x] WRITE failed!\n\n", AGENT_OFF_CARD_STAGE9);

		lib_disconnect(sockfd);
		goto Done;
	}

	fprintf(stderr, "Off Card BLACKLIST Process: [%02x] WRITE success\n\n", AGENT_OFF_CARD_STAGE9);

	return AGENT_ERR_OK;

Done:
	return AGENT_ERR_UNKNOWN;	
}


int lib_lnt_do_admin_card(void)
{
	int ret = LNT_ERROR;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	unsigned char key[16] = {0};
	unsigned char in_data[8] = {0};
	unsigned char out_data[8] = {0};
	
	lnt_qry_ticket_info_ack_t qry_ticket_info;
	lnt_admin_card_info_req_t a_card_info_req;
	lnt_admin_card_info_ack_t a_card_info_ack;
	lnt_admin_card_info_t a_card_info;
	lnt_consume_req_t consume;
	lib_lnt_trade_record_ack_t lnt_trade_rec;
	
	/* 查询票卡信息 */
	ret = LNT_ERROR;
	stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	memset(&qry_ticket_info, 0, sizeof(qry_ticket_info));
	ret =  dev_qry_ticket_info(&qry_ticket_info, &stat, 500);	
	if((ret <= 0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "%s:Qry Ticket Info ERROR!\n", __FUNCTION__);
		goto Done;
	}

	memcpy(key, qry_ticket_info.ticket.pid, 8);
	memcpy(&(key[8]), qry_ticket_info.ticket.lid, 8);

	/* 查询管理卡信息 */
	memset(&a_card_info_req, 0, sizeof(lnt_admin_card_info_req_t));
	memset(&a_card_info_ack, 0, sizeof(lnt_admin_card_info_ack_t));
	a_card_info_req.id = 0x68;
	ret = dev_qry_admin_card_info(&a_card_info_req, &a_card_info_ack, &stat, 300);
	if((ret <= 0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "%s:Qry Admin Card Info ERROR!\n", __FUNCTION__);
		goto Done;
	}

	//lib_printf_v2("Before ADMIN", (unsigned char *)&(a_card_info_ack.admin_36bytes), 36, 16);

	a_card_info_ack.admin_36bytes.A6 = 0xFE; //管理卡
	in_data[0] = a_card_info_ack.admin_36bytes.A6;  //A6-1
	memcpy(&(in_data[1]), &(a_card_info_ack.admin_36bytes.A7), 2);  //A7-2
	unsigned int t_data = a_card_info_ack.admin_36bytes.A9;
	memcpy(&(in_data[3]), &t_data, 3); //A9-3
	memcpy(&(in_data[6]), &(a_card_info_ack.admin_36bytes.A12), 2); //A12-2

	/* 加密数据 */
	lib_des3_encrypt(out_data, in_data, 8, key);

	/* 设置管理卡信息 */
	ret = LNT_ERROR;
	stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	memset(&a_card_info, 0, sizeof(lnt_admin_card_info_t));
	a_card_info.length = 36;
	a_card_info.id = 0x68;
	memcpy(&(a_card_info.admin_36bytes), &(a_card_info_ack.admin_36bytes), sizeof(a_card_info.admin_36bytes));
	memcpy(&(a_card_info.admin_36bytes.A10_TAC_KEY), out_data, 8); //拷贝TAC
	ret = dev_set_admin_card_info(&a_card_info, &stat, 300);
	if((ret <= 0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "%s:Set Admin Card Info ERROR!\n", __FUNCTION__);
		goto Done;
	}

	/* 消费 */
	ret = LNT_ERROR;
	stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	memset(&consume, 0, sizeof(lnt_consume_req_t));
	consume.amount = 0;
	consume.fee = 0;
	lib_lnt_utils_time_bcd_yymmddhhMMss(consume.time);
	memcpy(consume.phyiscalid, qry_ticket_info.ticket.pid, 8);
	consume.aty = LNT_ATY_RETURN;
	ret = dev_consume(&consume, &stat, 200);
	if((ret <= 0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "%s:Consume ERROR!\n", __FUNCTION__);
		goto Done;
	}

	/* 读取交易记录 */
	memset(&lnt_trade_rec, 0, sizeof(lib_lnt_trade_record_ack_t));
	ret = lib_lnt_get_trade_record(&lnt_trade_rec, &stat, 500);
	if((ret <= 0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "%s:Get Trade Record ERROR!\n", __FUNCTION__);
		goto Done;
	}

	#if 0
	/* 查询管理卡信息 */
	memset(&a_card_info_req, 0, sizeof(lnt_admin_card_info_req_t));
	memset(&a_card_info_ack, 0, sizeof(lnt_admin_card_info_ack_t));
	a_card_info_req.id = 0x68;
	ret = dev_qry_admin_card_info(&a_card_info_req, &a_card_info_ack, &stat, 300);
	if((ret <= 0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "%s:Qry Admin Card Info ERROR!\n", __FUNCTION__);
		goto Done;
	}

	lib_printf_v2("After ADMIN", (unsigned char *)&(a_card_info_ack.admin_36bytes), 36, 16);
	#endif

	return LNT_EOK;

Done:
	return LNT_ERROR;
}



enum LNT_ADMIN_CARD_TYPE lib_lnt_is_admin_card(void)
{
	int ret = LNT_ERROR;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	unsigned char key[16] = {0};
	unsigned char out_data[8] = {0};
	static unsigned char valid_flag = 0; //管理卡有效标记 0:无效 1:有效
	
	lnt_qry_ticket_info_ack_t qry_ticket_info;
	lnt_admin_card_info_req_t a_card_info_req;
	lnt_admin_card_info_ack_t a_card_info_ack;
	admin_card_encry_data_t a_card_encry_data;
	
	/* 查询票卡信息 */
	ret = LNT_ERROR;
	stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	memset(&qry_ticket_info, 0, sizeof(qry_ticket_info));
	ret =  dev_qry_ticket_info(&qry_ticket_info, &stat, 300);	
	if((ret <= 0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "%s:Qry Ticket Info ERROR!\n", __FUNCTION__);
		goto Done;
	}

	memcpy(key, qry_ticket_info.ticket.pid, 8);
	memcpy(&(key[8]), qry_ticket_info.ticket.lid, 8);

	/* 查询管理卡信息 */
	memset(&a_card_info_req, 0, sizeof(lnt_admin_card_info_req_t));
	memset(&a_card_info_ack, 0, sizeof(lnt_admin_card_info_ack_t));
	a_card_info_req.id = 0x68;
	ret = dev_qry_admin_card_info(&a_card_info_req, &a_card_info_ack, &stat, 300);
	if((ret <= 0) || (stat != LNT_MOC_ERROR_NORMAL))
	{
		fprintf(stderr, "%s:Qry Admin Card Info ERROR!\n", __FUNCTION__);
		goto Done;
	}	

	unsigned int A9 = 0;
	A9 = a_card_info_ack.admin_36bytes.A9; //启用时间
	
	/* 判断票种标记位 */
	if(a_card_info_ack.admin_36bytes.A6 != 0xFE)
	{
		printf("Not 0xFE!");
		goto Done;
	}

	/* 解密数据 */
	lib_des3_decrypt(out_data, a_card_info_ack.admin_36bytes.A10_TAC_KEY, 8, key);

	/* 数据比较 */
	memset(&a_card_encry_data, 0, sizeof(admin_card_encry_data_t));
	memcpy(&a_card_encry_data, out_data, sizeof(admin_card_encry_data_t)) ;
	
	if((a_card_encry_data.A6 != a_card_info_ack.admin_36bytes.A6) || \
		(a_card_encry_data.A7 != a_card_info_ack.admin_36bytes.A7) || \
	 	(a_card_encry_data.A9 != a_card_info_ack.admin_36bytes.A9) || \
	 	(a_card_encry_data.A12 != a_card_info_ack.admin_36bytes.A12))
	{
		printf("-----------Check Admin Card:Decrypt failed!");
		goto Done;
	}

#if 1
	/* 管理卡有效期判断 add by zjc at 2016-12-01 */
	static time_t start_times, now_times; 
	struct tm start_tm;
	static double offset_times_valid = 0, offset_times_real = 0;
	memset(&start_tm, 0, sizeof(struct tm));
	
	start_tm.tm_year = 2000 + lib_hex_to_bcd((A9 & 0x000000FF) >> 0) - 1900; //Years since 1900
	start_tm.tm_mon = lib_hex_to_bcd((A9 & 0x0000FF00) >> 8) - 1; //Month of year [0,11]
	start_tm.tm_mday = lib_hex_to_bcd((A9 & 0x00FF0000) >> 16); //Day of month [1,31]

	printf(" tm_year:%d\n", start_tm.tm_year);
	printf(" tm_mon:%d\n", start_tm.tm_mon);
	printf(" tm_mday:%d\n\n", start_tm.tm_mday);
	
	time(&now_times); //现在从1970年1月1日00时00分00秒至今所经过的秒数
	start_times = mktime(&start_tm); //启用日期从1970年1月1日00时00分00秒至今所经过的秒数
	offset_times_real = difftime(now_times, start_times); //启用日期到现在经过的秒数
	printf(" Start sec:%ld\n Now sec:%ld\n Offset sec:%lf\n\n", \
		start_times, now_times, offset_times_real);

	offset_times_valid = LNT_NTOHS(a_card_info_ack.admin_36bytes.A7) * (3600 * 24); //启用日期到刚好过期需经过的秒数
	printf(" offset_times_real:%lf\n offset_times_valid:%lf\n\n", offset_times_real, offset_times_valid);

	if((offset_times_valid >= offset_times_real) && (offset_times_real > 0))
	{
		printf("Check Admin Card:Valid!\n");
		valid_flag = 1; //有效
	}
	else
	{
		printf("Check Admin Card:Unvalid!\n");
		valid_flag = 0;
		return LNT_ADMIN_CARD_UNVALID; //无效
	}
#endif

	/* 管理卡类型 */
	if((a_card_info_ack.admin_36bytes.A8[0] == 0x03) && (valid_flag == 1))  //有设置权限
	{
		printf("------------Check Admin Card:LNT_ADMIN_CARD_TYPE_RW\n");
		return LNT_ADMIN_CARD_TYPE_RW;
	}
	else if(valid_flag == 1)
	{
		printf("------------Check Admin Card:LNT_ADMIN_CARD_TYPE_RD\n");
		return LNT_ADMIN_CARD_TYPE_RD;	
	}

Done:
	return LNT_ADMIN_CARD_TYPE_NOT;	  //非管理卡
}


#if 1 
/* 
** 非普通卡有效期判断 add by zjc at 2016-12-05 
*/
enum LNT_CARD_VALID_FLAG lib_lnt_is_valid_card(void)
{
		lib_lnt_rent_info_req_t rent_info_req;
		lib_lnt_rent_info_ack_t rent_info_ack;
		unsigned char stat = LNT_MOC_ERROR_UNKNOW;
		int ret = -1;

		memset(&rent_info_req, 0, sizeof(lib_lnt_rent_info_req_t));
		memset(&rent_info_ack, 0, sizeof(lib_lnt_rent_info_ack_t));
		rent_info_req.id = 0x68;
		
		ret = lib_lnt_qry_rent_info(&rent_info_req, &rent_info_ack, &stat, 300);
		if((ret <= 0) || (stat != LNT_MOC_ERROR_NORMAL))
		{
			fprintf(stderr, "%s:Qry Rent Info ERROR!\n", __FUNCTION__);
			goto Done;
		}	


		static time_t start_times, now_times, unvalid_times; 
		struct tm start_tm;
		static double offset_times_valid = 0, offset_times_real = 0, offset_times_unvalid = 0;
		int valid_flag = 0; //有效性标志 0:无效 1:有效 不能用static!!!
		
		memset(&start_tm, 0, sizeof(struct tm));
		unsigned int A9 = 0;
		memcpy(&A9, &rent_info_ack.rent.start_time, sizeof(rent_info_ack.rent.start_time));
		lib_printf("\nStart Time", (unsigned char *)&A9, 4);

		start_tm.tm_year = 2000 + lib_hex_to_bcd((A9 & 0x000000FF) >> 0) - 1900; //Years since 1900
		start_tm.tm_mon = lib_hex_to_bcd((A9 & 0x0000FF00) >> 8) - 1; //Month of year [0,11]
		start_tm.tm_mday = lib_hex_to_bcd((A9 & 0x00FF0000) >> 16); //Day of month [1,31]
		
		//启用日期
		printf("----------Start Date---------\n");
		printf(" tm_year:%d\n", start_tm.tm_year);
		printf(" tm_mon:%d\n", start_tm.tm_mon);
		printf(" tm_mday:%d\n\n", start_tm.tm_mday);
	
		printf("Ticket Type:%d, Limits:%d\n\n", rent_info_ack.rent.ticket_flag, LNT_NTOHS(rent_info_ack.rent.s_un.acc.accum));

		time(&now_times); //从1970年1月1日00时00分00秒 【至今】 所经过的秒数

		start_times = mktime(&start_tm); //从1970年1月1日00时00分00秒到 【启用日期】 所经过的秒数
		offset_times_real = difftime(now_times, start_times); //从启用日期到现在经过的秒数
		if(offset_times_real < 0) //启用时间晚于现在时间的判为无效
		{
			printf("Start Time is sooner than NOW, Unvalid!!!\n");
			valid_flag = 0; 
			goto Exit;
		}	
		
		if(rent_info_ack.rent.ticket_flag == MEMBER_TYPE_MONTHS) //月卡
		{
			start_tm.tm_mon += LNT_NTOHS(rent_info_ack.rent.s_un.acc.accum); 
			if(start_tm.tm_mon > 11) //月数大于12个月时算到往后某年的某个月
			{
				start_tm.tm_year += ((start_tm.tm_mon - 12) / 12) + 1;
				start_tm.tm_mon = (start_tm.tm_mon - 12) % 12; 
			}
		}
		else if(rent_info_ack.rent.ticket_flag == MEMBER_TYPE_YEARS) //年卡
		{
			start_tm.tm_year += LNT_NTOHS(rent_info_ack.rent.s_un.acc.accum); 
		}
	
		//失效日期
		printf("----------End Date---------\n");
		printf(" tm_year:%d\n", start_tm.tm_year);
		printf(" tm_mon:%d\n", start_tm.tm_mon);
		if(rent_info_ack.rent.ticket_flag != MEMBER_TYPE_DAYS)
			printf(" tm_mday:%d\n\n", start_tm.tm_mday);
		
		time(&now_times); //从1970年1月1日00时00分00秒 【至今】 所经过的秒数
	
		//天卡有效期判断
		if((rent_info_ack.rent.ticket_flag == MEMBER_TYPE_DAYS) || (rent_info_ack.rent.ticket_flag == MEMBER_TYPE_ADMIN)) //天卡 
		{
			start_times = mktime(&start_tm); //从1970年1月1日00时00分00秒到 【启用日期】 所经过的秒数
			offset_times_real = difftime(now_times, start_times); //从启用日期到现在经过的秒数
			printf(" Start sec:%ld\n Now sec:%ld\n offset_times_real:%lf\n\n", start_times, now_times, offset_times_real);
	
			offset_times_valid = LNT_NTOHS(rent_info_ack.rent.s_un.acc.accum) * (3600 * 24); //从启用日期到【刚好过期】需经过的秒数
			printf(" offset_times_real:%lf\n offset_times_valid:%lf\n\n", offset_times_real, offset_times_valid);
	
			if((offset_times_real < offset_times_valid) && (offset_times_real > 0)) //启用时间晚于现在时间的判为无效
			{
				valid_flag = 1; 
			}
		}
		else if(rent_info_ack.rent.ticket_flag != MEMBER_TYPE_TIMES) //不是次卡，即月卡和年卡
		{
			unvalid_times = mktime(&start_tm); //从1970年1月1日00时00分00秒到 【刚好过期】 需所经过的秒数
			printf(" unvalid_times:%ld\n now_times:%ld\n\n", unvalid_times, now_times);
	
			offset_times_unvalid = difftime(now_times, unvalid_times); //从【刚好过期】到现在经过的秒数
			printf(" offset_times_unvalid:%lf\n\n", offset_times_unvalid);
	
			if(offset_times_unvalid <= 0)
			{
				valid_flag = 1; 
			}
		}
		else if((rent_info_ack.rent.ticket_flag == MEMBER_TYPE_TIMES) && (LNT_NTOHS(rent_info_ack.rent.s_un.acc.accum) > 0)) //次卡
		{
			valid_flag = 1; 
		}

Exit:
		if(valid_flag == 1)
		{
			printf("---------Check Card:Valid!\n");
			return LNT_CARD_VALID;
		}
		else if(rent_info_ack.rent.ticket_flag != MEMBER_TYPE_NORMAL)
		{
			printf("---------Check Card:Unvalid!\n");
			return LNT_CARD_UNVALID;
		}

Done:
	return LNT_CARD_UNVALID;
}
#endif


int lib_lnt_init(lib_lnt_config_t *config)
{
	if(config == NULL)
		return LNT_ERROR;
	
	int err = LNT_ERROR;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lib_lnt_core_init_req_t core_req;
	lib_lnt_core_init_ack_t core_ack;
	lib_lnt_parameter_set_req_t lnt_paraset_req;
	
	err = dev_init(config->tty, config->baudrate);
	if(err < 0)
	{
		fprintf(stderr, "%s:device init failed!\n", __FUNCTION__);
		return err;
	}

	memset(&lnt_paraset_req, 0, sizeof(lib_lnt_parameter_set_req_t));
	memset(&core_req, 0, sizeof(lib_lnt_core_init_req_t));
	memset(&core_ack, 0, sizeof(lib_lnt_core_init_ack_t));

	lnt_paraset_req.conpa = config->conpa;
	lnt_paraset_req.cvad = config->cvad;

	memcpy(&(core_req.spno), &(config->spno), 2);

	lib_lnt_set_parameter(&lnt_paraset_req, &stat, 100); //参数设置

	lib_lnt_core_init(&core_req, &core_ack, &stat, 200);  //核心初始化

//	lib_printf("PSAM", &(core_ack.psam), 4);
	
	memcpy(&(config->psam), &(core_ack.psam), 4);  //PSAM
	
	__lnt_register_stat_set(LNT_REGISTER_STAT_INIT);  //设置岭南通签到初始化

#if 1 //新协议是否需要PKI?
	lnt_r_init_ack_t r_init_ack;
	memset(&r_init_ack,0,sizeof(lnt_r_init_ack_t));

	dev_r_init(&r_init_ack, (unsigned char *)&stat, 300);
	memcpy(config->pki_RO, r_init_ack.pki, 4);
	lib_printf("pki_RO", &config->pki_RO, 4);
#endif

	memcpy(&g_lnt_conf, config, sizeof(lib_lnt_config_t));

	g_lnt_register_notify_ptr = (lnt_notify_ptr_t *)malloc(sizeof(lnt_notify_ptr_t));
	g_lnt_register_notify_ptr->mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	g_lnt_register_notify_ptr->cond = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	
	g_lnt_register_notify_ptr->notify = LNT_REGISTER_STAT_INIT;
		
	pthread_mutex_init(g_lnt_register_notify_ptr->mutex, NULL);
	pthread_cond_init(g_lnt_register_notify_ptr->cond, NULL);
	
	g_register_proc_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(g_register_proc_mutex, NULL);

#if 1
#ifdef LNT_EXE_DEBUG 
	__lnt_register_entry(config);
#else
	pthread_t  lnt_reg_pid;
	err = lib_normal_thread_create(&lnt_reg_pid, __lnt_register_entry, config);
#endif
#endif
	return err;  
}

int lib_lnt_release(void)
{
	pthread_mutex_destroy(g_register_proc_mutex);
	pthread_mutex_destroy(g_lnt_register_notify_ptr->mutex);
	pthread_cond_destroy(g_lnt_register_notify_ptr->cond);
	
	if(g_lnt_register_notify_ptr->mutex != NULL)
	{
		free(g_lnt_register_notify_ptr->mutex);
		g_lnt_register_notify_ptr->mutex = NULL;
	}

	if(g_lnt_register_notify_ptr->cond!= NULL)
	{
		free(g_lnt_register_notify_ptr->cond);
		g_lnt_register_notify_ptr->cond= NULL;
	}

	if(g_lnt_register_notify_ptr != NULL)
	{
		free(g_lnt_register_notify_ptr);
		g_lnt_register_notify_ptr = NULL;
	}
	
	dev_release();
	
	return LNT_EOK;
}

int lib_lnt_get_firmware_version(char *version)
{
	if(version == NULL)
		return LIB_LNT_ERROR;

	strcpy(version, LNT_FW_VER);

	return LIB_LNT_EOK;
}

int lib_lnt_set_TTY_BAUDRATE(const char tty[32], const unsigned int baudrate)
{
	if(tty == NULL)
		return LNT_ERROR;

	strcpy(g_lnt_conf.tty, tty);
	g_lnt_conf.baudrate = baudrate;

	return LNT_EOK;
}


int lib_lnt_set_IP_PORT(const char ipaddr[16], const unsigned short port)
{
	if(ipaddr == NULL)
		return LNT_ERROR;

	memcpy(g_lnt_conf.ipaddr, ipaddr, 16);
	g_lnt_conf.port = port;

	return LNT_EOK;
}

int lib_lnt_set_SPNO_CONPA_CVAD_USERID(const unsigned char spno[2], const unsigned char conpa, const unsigned char cvad, const unsigned char userid[16])
{
	if(spno == NULL)
		return LNT_ERROR;

	memcpy(g_lnt_conf.spno, spno, 2);
	g_lnt_conf.conpa = conpa;
	g_lnt_conf.cvad = cvad;
	memcpy(g_lnt_conf.userid, userid, 16);

	return LNT_EOK;
}

int lib_lnt_get_config(lib_lnt_config_t *config)
{
	if(config == NULL)
		return LNT_ERROR;

	memcpy(config, &g_lnt_conf, sizeof(lib_lnt_config_t));

	return LNT_EOK;	
}

void lib_lnt_utils_time_bcd_yymmddhhMMss(unsigned char tm[6])
{
	unsigned short year = 0;
	unsigned char mon = 0;	
	time_t timep;
	struct tm *ptm = NULL;

	time(&timep);
	ptm = localtime(&timep);
	if(ptm != NULL)
	{
		year = 1900 + ptm->tm_year;
		mon = 1 + ptm->tm_mon;
		tm[0] = lib_dec_to_bcd(year % 100);
		tm[1] = lib_dec_to_bcd(mon);
		tm[2] = lib_dec_to_bcd(ptm->tm_mday);
		tm[3] = lib_dec_to_bcd(ptm->tm_hour);
		tm[4] = lib_dec_to_bcd(ptm->tm_min);
		tm[5] = lib_dec_to_bcd(ptm->tm_sec);
	}	
}

void lib_lnt_utils_time_bcd_yymmddhhMM(unsigned char tm[5])
{
	unsigned short year = 0;
	unsigned char mon = 0;	
	time_t timep;
	struct tm *ptm = NULL;

	time(&timep);
	ptm = localtime(&timep);
	if(ptm != NULL)
	{
		year = 1900 + ptm->tm_year;
		mon = 1 + ptm->tm_mon;
		tm[0] = lib_dec_to_bcd(year % 100);
		tm[1] = lib_dec_to_bcd(mon);
		tm[2] = lib_dec_to_bcd(ptm->tm_mday);
		tm[3] = lib_dec_to_bcd(ptm->tm_hour);
		tm[4] = lib_dec_to_bcd(ptm->tm_min);
	}	
}

void lib_lnt_utils_time_bcd_yymmdd(unsigned char tm[3])
{
	unsigned short year = 0;
	unsigned char mon = 0;	
	time_t timep;
	struct tm *ptm = NULL;

	time(&timep);
	ptm = localtime(&timep);
	if(ptm != NULL)
	{
		year = 1900 + ptm->tm_year;
		mon = 1 + ptm->tm_mon;
		tm[0] = lib_dec_to_bcd(year % 100);
		tm[1] = lib_dec_to_bcd(mon);
		tm[2] = lib_dec_to_bcd(ptm->tm_mday);
	}	
}







int lib_lnt_get_version(lib_lnt_getversion_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_get_version(ack, stat, msec);
}

int lib_lnt_core_init(lib_lnt_core_init_req_t *req, lib_lnt_core_init_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_core_init(req, ack, stat, msec);
}

int lib_lnt_set_parameter(lib_lnt_parameter_set_req_t *req, unsigned char *stat, const unsigned int msec)
{
	return dev_set_parameter(req, stat, msec);
}

int lib_lnt_qry_ticket_info(lib_lnt_qry_ticket_info_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_qry_ticket_info(ack, stat, msec);
}

int lib_lnt_qry_rent_info(lib_lnt_rent_info_req_t *req, lib_lnt_rent_info_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_qry_rent_info(req, ack, stat, msec);
}

int lib_lnt_set_rent_info(lib_lnt_rent_info_t *rent, unsigned char *stat, const unsigned int msec)
{
	return dev_set_rent_info(rent, stat, msec);
}

int lib_lnt_get_history_record(lib_lnt_history_record_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_get_history_record(ack, stat, msec);
}

int lib_lnt_set_blacklist_flag(lib_lnt_blacklist_record_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_set_blacklist_flag(ack, stat, msec);
}

int lib_lnt_get_trade_record(lib_lnt_trade_record_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_get_trade_record(ack, stat, msec);
}

int lib_lnt_consume(lib_lnt_consume_req_t *req, unsigned char *stat, const unsigned int msec)
{
	return dev_consume(req, stat, msec);
}



int lib_lnt_CLA_INS_pkg(void *s_ptr, const unsigned int s_len, void *d_ptr, const unsigned int d_len, unsigned char *d_stat, const unsigned int msec)
{
	return dev_CLA_INS_pkg(s_ptr, s_len, d_ptr, d_len, d_stat, msec);
}


int lib_lnt_r_init(lnt_r_init_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_r_init(ack, stat, msec);
}

int lib_lnt_r_ac_login_1(lnt_r_ac_login_1_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_r_ac_login_1(ack, stat, msec);
}






int lib_lnt_r_deposit_init(lnt_r_deposit_init_req_t *req, lnt_r_deposit_init_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_r_deposit_init(req, ack, stat, msec);
}

int lib_lnt_r_deposit_process(lnt_r_deposit_process_req_t *req, lnt_r_deposit_process_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	return dev_r_deposit_process(req, ack, stat, msec);
}






static FILE *g_lnt_log_fp = NULL;



int LIB_LNT_log_open(void)
{
	char s_lnt_log_path[64] = {0};

	 time_t timep;
	 time(&timep);
	struct tm *ptm = localtime(&timep);
	if(ptm == NULL)
		return LNT_ERROR;
		              			   
	sprintf(s_lnt_log_path, "/opt/logpath/lntlog_%02d%02d%02d.log", 1900+ptm->tm_year, 1+ptm->tm_mon, ptm->tm_mday);
	fprintf(stderr, "LNT LOG PATH:%s\n", s_lnt_log_path);

	g_lnt_log_fp = fopen(s_lnt_log_path, "a");
	if(g_lnt_log_fp == NULL)
		return LNT_ERROR;

	return LNT_EOK;
}


void LIB_LNT_log_close(void)
{
	if(g_lnt_log_fp != NULL)
	{
		fclose(g_lnt_log_fp);
		g_lnt_log_fp = NULL;
	}
}

int LIB_LNT_log_vsprintf(char *fmt, ...)
{
	char s_lnt_log[512] = {0};
	char s_log[512] = {0};
	time_t timep;
	time(&timep);
	struct tm *ptm = localtime(&timep);
	if(ptm == NULL)
		return -1;

	int cnt;
	va_list argptr; 

	va_start(argptr, fmt);
	cnt = vsprintf(s_log, fmt, argptr); 
	va_end(argptr);
	
	sprintf(s_lnt_log, "%02d-%02d-%02d %02d:%02d:%02d  %s", 1900+ptm->tm_year, 1+ptm->tm_mon, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, s_log);
	fputs(s_lnt_log, g_lnt_log_fp);
	fflush(g_lnt_log_fp);

	return cnt;
}

int LIB_LNT_log_vsprintf_debug(const int debug, char *fmt, ...)
{
	if(debug > 0)
	{
		char s_lnt_log[512] = {0};
		char s_log[512] = {0};
		time_t timep;
		time(&timep);
		struct tm *ptm = localtime(&timep);
		if(ptm == NULL)
			return -1;

		int cnt;
		va_list argptr; 

		va_start(argptr, fmt);
		cnt = vsprintf(s_log, fmt, argptr); 
		va_end(argptr);
		
		sprintf(s_lnt_log, "DEBUG-%02d-%02d-%02d %02d:%02d:%02d  %s", 1900+ptm->tm_year, 1+ptm->tm_mon, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, s_log);
		fputs(s_lnt_log, g_lnt_log_fp);
		fflush(g_lnt_log_fp);

		return cnt;
	}

	return LNT_EOK;
}




/*读卡器操作失败后的初始化操作,这样才可使读卡器马上进入正常状态*/ //add by zjc
int lnt_init(lib_lnt_config_t *config)
{
	if(config == NULL)
		return LNT_ERROR;
	
	int err = LNT_ERROR;
	unsigned char stat = LNT_MOC_ERROR_CARD_OP_FAIL;
	lib_lnt_core_init_req_t core_req;
	lib_lnt_core_init_ack_t core_ack;
	lib_lnt_parameter_set_req_t lnt_paraset_req;
	
	err = dev_init(config->tty, config->baudrate);
	if(err < 0)
	{
		fprintf(stderr, "%s:device init failed!\n", __FUNCTION__);
		return err;
	}

	memset(&lnt_paraset_req, 0, sizeof(lib_lnt_parameter_set_req_t));
	memset(&core_req, 0, sizeof(lib_lnt_core_init_req_t));
	memset(&core_ack, 0, sizeof(lib_lnt_core_init_ack_t));

	lnt_paraset_req.conpa = config->conpa;
	lnt_paraset_req.cvad = config->cvad;

	memcpy(&(core_req.spno), &(config->spno), 2);

	lib_lnt_set_parameter(&lnt_paraset_req, &stat, 200); //参数设置

	err = lib_lnt_core_init(&core_req, &core_ack, &stat, 300);  //核心初始化

	return err;
}


#if 1
/* 岭南通读卡器升级 add by zjc at 2016-07-22 */
int lib_lnt_reader_update(char *firmware_path)
{
	return dev_reader_update(firmware_path);
}
#endif

#if 0
/* 保存读卡器固件版本号到文本 add by zjc at 2016-08-17 */
#define LNT_FIRMWARE_PATH				"/opt/config/lnt_firmware_version_file.txt"

void  __lnt_firmware_version_put(char version[24])
{
	FILE *fp = NULL;

	fp = fopen(LNT_FIRMWARE_PATH, "wt"); /* 写文本 */
	if(fp != NULL)
	{
		if(fputs(version, fp) != NULL)
		{
			fprintf(stderr, "Bicycle_gui Lnt Firmware Version put: %s\n", version);

			fflush(fp);
		}

		fclose(fp);
	}
}
#endif


