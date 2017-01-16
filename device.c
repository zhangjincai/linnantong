#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#include "lib_general.h"
#include "device.h"
#include "lib_lnt.h"

#include "gpio_ctrl.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>




#define LNT_WR_HEAD					0xBA
#define LNT_RD_HEAD					0xBD

#define LNT_PROT_RX_LEN				(5)				

#define USING_WR_N				
#define USING_RD				


struct var_data
{
	unsigned int idx;
	unsigned int total_len;
	unsigned char data[1024];
}__attribute__((packed));

struct vla_data
{
	unsigned char cmd;
	unsigned char cla;
	unsigned char ins;
	unsigned char p1;
	unsigned char p2;
	unsigned char leorlc;
}__attribute__((packed));
typedef struct vla_data vla_data_t;

static lib_serial_t g_serial;
static struct var_data g_var_data;

#define VAR_DATA				g_var_data.data
#define VAR_DATA_LEN		g_var_data.total_len

static unsigned char __calc_check_sum(unsigned char *data, const unsigned int len)
{
	int i;
	unsigned char ck = 0;

	for(i = 0; i< len; i++)
	{
		ck = ck ^ data[i];
	}

	return ck;
}

static void __var_data_hd(void)
{
	memset(&g_var_data, 0, sizeof(struct var_data));
	g_var_data.data[0] = LNT_WR_HEAD;
	g_var_data.total_len += 1;
}

static void __var_data_cmd(const unsigned char cmd)
{
	g_var_data.data[2] = cmd;
	g_var_data.idx = 3;
	g_var_data.total_len += 2;
}

static void __var_data_crc(void)
{
	g_var_data.data[1] = g_var_data.idx -1;
	g_var_data.data[g_var_data.idx] = __calc_check_sum(g_var_data.data, g_var_data.total_len);
	g_var_data.total_len += 1;
}

#if 0
static void __var_data_char(const unsigned char ch)
{
	g_var_data.data[g_var_data.idx] = ch;
	g_var_data.idx += 1;
	g_var_data.total_len += 1;
}

static void __var_data_short(const unsigned short sot)
{
	unsigned short m_sot = htons(sot);
	memcpy(&(g_var_data.data[g_var_data.idx]), &m_sot, 2);
	g_var_data.idx += 2;
	g_var_data.total_len += 2;		
}

static void __var_data_int(const unsigned short it)
{
	unsigned int m_it = htonl(it);
	memcpy(&(g_var_data.data[g_var_data.idx]), &m_it, 4);
	g_var_data.idx += 4;
	g_var_data.total_len += 4;		
}
#endif

static void __var_data_set(void *ptr, const unsigned int size)
{
	memcpy(&(g_var_data.data[g_var_data.idx]), ptr, size);
	g_var_data.idx += size;
	g_var_data.total_len += size;
}

static void __var_data_printf(void)
{
	int i;
	
	//fprintf(stderr, "idx: %d\n", g_var_data.idx);
	//fprintf(stderr, "total_len: %d\n", g_var_data.total_len);

	for(i = 0; i < g_var_data.total_len; i++)
	{
		if(i % 16 == 0)
			fprintf(stderr, "\n");
		fprintf(stderr, "%02x ", g_var_data.data[i]);
	}

	fprintf(stderr, "\n");
}

static void __cla_var_data_hd(vla_data_t *vla)
{
	memset(&g_var_data, 0, sizeof(struct var_data));
	g_var_data.data[0] = LNT_WR_HEAD;
	memcpy(&g_var_data.data[2], vla, sizeof(struct vla_data));
	g_var_data.idx = 8;
	g_var_data.total_len += 8;	
}

static void __cla_var_data_set(void *ptr, const unsigned int size)
{
	memcpy(&(g_var_data.data[g_var_data.idx]), ptr, size);
	g_var_data.idx += size;
	g_var_data.total_len += size;	
}

static void __cla_var_data_crc(void)
{
	g_var_data.data[1] = g_var_data.idx -1;
	g_var_data.data[g_var_data.idx] = __calc_check_sum(g_var_data.data, g_var_data.total_len);
	g_var_data.total_len += 1;
}

static void __cla_var_data_printf(void)
{
	int i;
	
	for(i = 0; i < g_var_data.total_len; i++)
	{
		if(i % 16 == 0)
			fprintf(stderr, "\n");
		fprintf(stderr, "%02x ", g_var_data.data[i]);
	}

	fprintf(stderr, "\n");
}

/* 读卡器升级专用 add by zjc at 2016-07-22 */
static void __CLA_INS_DATA_HEADER(int stage)
{
	memset(&g_var_data, 0, sizeof(struct var_data));
 	g_var_data.data[0] = LNT_WR_HEAD;  //header
	g_var_data.data[2] = 0xFF;  //command
	g_var_data.data[3] = stage;  //sub command
	
	g_var_data.idx = 4;
	g_var_data.total_len += 4;	
}

static void __CLA_INS_DATA_HD(void)
{
	memset(&g_var_data, 0, sizeof(struct var_data));
 	g_var_data.data[0] = LNT_WR_HEAD;  //header
	g_var_data.data[2] = 0x40;  //command
	
	g_var_data.idx = 3;
	g_var_data.total_len += 3;	
}

static void __CLA_INS_DATA_SET(void *ptr, const unsigned int size)
{
	memcpy(&(g_var_data.data[g_var_data.idx]), ptr, size);
	g_var_data.idx += size;
	g_var_data.total_len += size;	
}

static void __CLA_INS_DATA_CRC(void)
{
	g_var_data.data[1] = g_var_data.idx -1;
	g_var_data.data[g_var_data.idx] = __calc_check_sum(g_var_data.data, g_var_data.total_len);
	g_var_data.total_len += 1;	
}

#if 1
static void __CLA_INS_DATA_PRINTF(void)
{
	int i;
	
	for(i = 0; i < g_var_data.total_len; i++)
	{
		if(i % 16 == 0)
			fprintf(stderr, "\n");
		fprintf(stderr, "%02x ", g_var_data.data[i]);
	}

	fprintf(stderr, "\n");
}
#endif


#if 0
static int __dev_serial_write_lock()
{

	lib_mutex_lock(lib_mutex_t * mutex)

	int n = lib_writen();

	lib_mutex_unlock()

	return n;
}
#endif


int dev_init(char *tty, const unsigned int baudrate)
{
	int err = LIB_GE_ERROR;
	
	strcpy(g_serial.pathname, tty);
	g_serial.flags = O_RDWR;
	g_serial.speed = baudrate;
	g_serial.databits = 8;
	g_serial.stopbits = 1;
	err = lib_serial_init(&g_serial);
	if(err == LIB_GE_ERROR)
	{
		//SYS_LOG_ERR("%s:serial init failed!\n", __FUNCTION__);
		return -1;
	}

	return 0;
}

void dev_release(void)
{
	lib_serial_close(&g_serial);
}




int dev_get_version(lnt_getversion_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = LNT_PROT_RX_LEN + sizeof(lnt_getversion_ack_t) + 32;
	
	__var_data_hd();
	__var_data_cmd(LNT_GETVERSION);
	__var_data_crc();

	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	
	//printf("write ret:%d\n", ret);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		
		//lib_printf_v2("get ver read",rxbuf,ret,16);
		if(ret > 0)
		{	
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_GETVERSION))
				return LNT_ERROR;

			rxlen = ret - 2;
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_getversion_ack_t));
			
			return ret;
		}
	}
	
	return LNT_ERROR;
}

int dev_core_init(lnt_core_init_req_t *req, lnt_core_init_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if((req == NULL) ||(ack == NULL))
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len= LNT_PROT_RX_LEN + sizeof(lnt_core_init_ack_t);
	
	__var_data_hd();
	__var_data_cmd(LNT_CORE_INIT);
	__var_data_set(req, sizeof(lnt_core_init_req_t));
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_CORE_INIT))
				return LNT_ERROR;

			rxlen = ret - 2;
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;
			
			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_core_init_ack_t));
			
			return ret;
		}
	}
	
	return LNT_ERROR;
}

int dev_set_parameter(lnt_parameter_set_req_t *req, unsigned char *stat, const unsigned int msec)
{
	if(req == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len= LNT_PROT_RX_LEN;
	
	__var_data_hd();
	__var_data_cmd(LNT_SET_PARAM);
	__var_data_set(req, sizeof(lnt_parameter_set_req_t));
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_SET_PARAM))
				return LNT_ERROR;

			rxlen = ret - 2;
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;			

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			return ret;
		}
	}
	
	return LNT_ERROR;;	
}

int dev_qry_ticket_info(lnt_qry_ticket_info_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = sizeof(lnt_qry_ticket_info_ack_t) + LNT_PROT_RX_LEN;
	
	__var_data_hd();
	__var_data_cmd(LNT_QRY_TICKET_INFO);
	__var_data_crc();

	//__var_data_printf();

	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_QRY_TICKET_INFO))
				return LNT_ERROR;

			rxlen = ret - 2;  
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_qry_ticket_info_ack_t));
			
			return ret;
		}
	}
	
	return LNT_ERROR;	
}

int dev_qry_rent_info(lnt_rent_info_req_t *req, lnt_rent_info_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if((req == NULL) ||(ack == NULL))
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = sizeof(lnt_rent_info_ack_t) + LNT_PROT_RX_LEN;
		
	__var_data_hd();
	__var_data_cmd(LNT_QRY_RENT_INFO);
	__var_data_set(req, sizeof(lnt_rent_info_req_t));
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_QRY_RENT_INFO))
				return LNT_ERROR;

			rxlen = ret - 2;  
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;
			
			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_rent_info_ack_t));
			
			return ret;
		}
	}
	
	return LNT_ERROR;	
}

int dev_set_rent_info(lnt_rent_info_t *rent, unsigned char *stat, const unsigned int msec)
{
	if(rent == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = LNT_PROT_RX_LEN;

	rent->rent.app_locker = 0x00;  //0x00:正常, 0x01:应用锁定,!!!!!谨慎写入！

	__var_data_hd();
	__var_data_cmd(LNT_SET_RENT_INFO);
	__var_data_set(rent, sizeof(lnt_rent_info_t));
	__var_data_crc();

	//__var_data_printf();

	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_SET_RENT_INFO))
				return LNT_ERROR;

			rxlen = ret - 2;  
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;			

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			return ret;
		}
	}

	return LNT_ERROR;		
}

int dev_set_blacklist_flag(lnt_blacklist_record_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = LNT_PROT_RX_LEN + sizeof(lnt_blacklist_record_ack_t);
	
	__var_data_hd();
	__var_data_cmd(LNT_SET_BL_FLAG);
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_SET_BL_FLAG))
				return LNT_ERROR;

			rxlen = ret - 2;
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;
			
			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_blacklist_record_ack_t));
			
			return ret;
		}
	}
	
	return LNT_ERROR;	
}

int dev_get_history_record(lnt_history_record_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	
	__var_data_hd();
	__var_data_cmd(LNT_GET_HIS_RECORD);
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_serial_send(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, sizeof(rxbuf), msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_GET_HIS_RECORD))
				return -1;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return -1;
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_history_record_ack_t));
			
			return ret;
		}
	}
	
	return ret;		
}

int dev_get_trade_record(lnt_trade_record_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = sizeof(lnt_trade_record_ack_t) + LNT_PROT_RX_LEN;
	
	__var_data_hd();
	__var_data_cmd(LNT_GET_TRANS);
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_GET_TRANS))
				return LNT_ERROR;

			rxlen = ret - 2;  
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;
			
			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_trade_record_ack_t));
			
			return ret;
		}
	}
	
	return LNT_ERROR;		
}

int dev_consume(lnt_consume_req_t *req, unsigned char *stat, const unsigned int msec)
{
	if(req == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = LNT_PROT_RX_LEN;
	
	__var_data_hd();
	__var_data_cmd(LNT_CONSUME);
	__var_data_set(req, sizeof(lnt_consume_req_t));
	__var_data_crc();

	//__var_data_printf();

	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	//lib_printf("consume write data",VAR_DATA,ret);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		//printf("consume read ret:%d\n",ret);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_CONSUME))
				return LNT_ERROR;

			rxlen = ret - 2;  
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;
			
			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			return ret;
		}
	}

	return LNT_ERROR;	
}




















#if 0
int dev_r_init(lnt_r_init_ack_t *ack, unsigned char *stat, unsigned short *sw, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned short tsw = 0;
	unsigned char rxbuf[1024] = {0};
	vla_data_t vla;

	vla.cmd = LNT_R_CMD;
	vla.cla = LNT_R_CLA;
	vla.ins = 0x10;
	vla.p1 = 0x01;
	vla.p2 = 0x02;
	vla.leorlc = 0x00;

	__cla_var_data_hd(&vla);
	__cla_var_data_crc();
	//__cla_var_data_printf();
	
	ret = lib_serial_send(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, sizeof(rxbuf), msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_R_CMD))
				return -1;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return -1;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			if(sw != NULL)
			{
				memcpy(&tsw, &(rxbuf[ret - 3]), 2);
				*sw = LNT_NTOHS(tsw);
			}
				
			memcpy(ack, &rxbuf[4], rxbuf[1] - 5);

			return ret;		
		}
	}

	return ret;	
}
#endif


int dev_CLA_INS_pkg(void *s_ptr, const unsigned int s_len, void *d_ptr, const unsigned int d_len, unsigned char *d_stat, const unsigned int msec)
{	
	if((s_ptr == NULL) || (d_ptr == NULL))
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len= LNT_PROT_RX_LEN + d_len; 
		
	__CLA_INS_DATA_HD();
	__CLA_INS_DATA_SET(s_ptr, s_len);
	__CLA_INS_DATA_CRC();
	
	//__CLA_INS_DATA_PRINTF();

	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_R_CMD))
				return LNT_ERROR;

			rxlen = ret - 2;
			if(rxlen != rxbuf[1])  
				return LNT_ERROR;
			
			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(d_stat != NULL)
				*d_stat = rxbuf[3];

			if(d_len < rxbuf[1] - 3)
				return -5; //内存不足

			memcpy(d_ptr, &rxbuf[4], rxbuf[1] - 3);

			return ret;		
		}
	}
	
	return LNT_ERROR;
}




/* 读卡器升级专用 add by zjc at 2016-07-22 */
int dev_CLA_INS_packet(void *s_ptr, const unsigned int s_len, void *d_ptr, const unsigned int d_len, unsigned char *d_stat, unsigned int stage, const unsigned int msec)
{
	if((s_ptr == NULL) || (d_ptr == NULL))
		return -1;

	int ret = -1;
	unsigned char rxbuf[256] = {0};

	if(stage)
		__CLA_INS_DATA_HEADER(stage);
	else
		__CLA_INS_DATA_HD();
	
	__CLA_INS_DATA_SET(s_ptr, s_len);
	__CLA_INS_DATA_CRC();

	unsigned int expect_len = LNT_PROT_RX_LEN + d_len; 
	//__CLA_INS_DATA_PRINTF();

	ret = lib_serial_send(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	
	//lib_printf_v2("send data",VAR_DATA,ret,16);
	if(ret > 0)
	{	
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_R_CMD_UPDATE))
				return -1;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return -1;
			
			if(d_stat != NULL)
				*d_stat = rxbuf[3];

			if(d_len < rxbuf[1] - 3)
				return -5; //内存不足

			memcpy(d_ptr, &rxbuf[4], rxbuf[1] - 3);

			return ret;		
		}
	}
	
	return ret;
}

int dev_r_init(lnt_r_init_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	vla_data_t vla;
	unsigned char rxlen = 0;
	unsigned int expect_len= LNT_PROT_RX_LEN + sizeof(lnt_r_init_ack_t); 
	
	vla.cmd = LNT_R_CMD;
	vla.cla = LNT_R_CLA;
	vla.ins = 0x10;
	vla.p1 = 0x01;
	vla.p2 = 0x02;
	vla.leorlc = 0x00;

	__cla_var_data_hd(&vla);
	__cla_var_data_crc();
	//__cla_var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_R_CMD))
				return LNT_ERROR;

			rxlen = ret - 2;
			if(rxlen != rxbuf[1])  
				return LNT_ERROR;
			
			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], rxbuf[1] - 3);

			return ret;		
		}
	}

	return LNT_ERROR;	
}


int dev_r_ac_login_1(lnt_r_ac_login_1_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	vla_data_t vla;

	vla.cmd = LNT_R_CMD;
	vla.cla = LNT_R_CLA;
	vla.ins = 0xC8;
	vla.p1 = 0x00;
	vla.p2 = 0x00;
	vla.leorlc = 136;

	__cla_var_data_hd(&vla);
	__cla_var_data_crc();
	__cla_var_data_printf();
	
	ret = lib_serial_send(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, sizeof(rxbuf), msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_R_CMD))
				return -1;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return -1;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], rxbuf[1] - 3);

			return ret;	
		}
	}

	return ret;		
}

int dev_r_ac_login_2(lnt_r_ac_login_2_req_t *req, lnt_r_ac_login_2_ack_t *ack, unsigned char *stat, unsigned short *sw, const unsigned int msec)
{
	if((req == NULL) || (ack == NULL))
		return -1;

	int ret = -1;
	unsigned short tsw = 0;
	unsigned char rxbuf[1024] = {0};
	vla_data_t vla;

	vla.cmd = LNT_R_CMD;
	vla.cla = LNT_R_CLA;
	vla.ins = 0xC9;
	vla.p1 = 0x00;
	vla.p2 = 0x00;
	vla.leorlc = 136;

	__cla_var_data_hd(&vla);
	__cla_var_data_set(req, sizeof(lnt_r_ac_login_2_req_t));
	__cla_var_data_crc();
	
	ret = lib_serial_send(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, sizeof(rxbuf), msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_R_CMD))
				return -1;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return -1;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			if(sw != NULL)
			{
				memcpy(&tsw, &(rxbuf[ret - 3]), 2);
				*sw = LNT_NTOHS(tsw);
			}
				
			memcpy(ack, &rxbuf[4], rxbuf[1] - 5);
			
			return ret;	
		}
	}

	return ret;	
}

int dev_r_pub_qry_his(lnt_r_pub_qry_his_ack_t *ack, unsigned char *stat, unsigned short *sw, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned short tsw = 0;
	unsigned char rxbuf[1024] = {0};
	vla_data_t vla;

	vla.cmd = LNT_R_CMD;
	vla.cla = LNT_R_CLA;
	vla.ins = 0x92;
	vla.p1 = 0x00;
	vla.p2 = 0x00;
	vla.leorlc = 0x00;

	__cla_var_data_hd(&vla);
	__cla_var_data_crc();
	
	ret = lib_serial_send(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, sizeof(rxbuf), msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_R_CMD))
				return -1;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return -1;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			if(sw != NULL)
			{
				memcpy(&tsw, &(rxbuf[ret - 3]), 2);
				*sw = LNT_NTOHS(tsw);
			}
				
			memcpy(ack, &rxbuf[4], rxbuf[1] - 5);
			
			return ret;	
		}
	}

	return ret;	
}

int dev_r_pub_qry_physics_info_NoEncry(lnt_r_pub_qry_physics_info_NoEncry_ack_t *ack, unsigned char *stat, unsigned short *sw, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned short tsw = 0;
	unsigned char rxbuf[1024] = {0};
	vla_data_t vla;
	unsigned char dataXX;
		
	vla.cmd = LNT_R_CMD;
	vla.cla = LNT_R_CLA;
	vla.ins = 0x93;
	vla.p1 = 0x00;   //0x00:不加密
	vla.p2 = 0x00;
	vla.leorlc = 0x00;

	__cla_var_data_hd(&vla);
	__cla_var_data_set(&dataXX, 1);
	__cla_var_data_crc();
	
	ret = lib_serial_send(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, sizeof(rxbuf), msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_R_CMD))
				return -1;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return -1;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			if(sw != NULL)
			{
				memcpy(&tsw, &(rxbuf[ret - 3]), 2);
				*sw = LNT_NTOHS(tsw);
			}
				
			memcpy(ack, &rxbuf[4], rxbuf[1] - 5);
			
			return ret;	
		}
	}

	return ret;		
}

int dev_r_pub_qry_physics_info_Encry(lnt_r_pub_qry_physics_info_Encry_ack_t *ack, unsigned char *stat, unsigned short *sw, const unsigned int msec)
{
	if(ack == NULL)
		return -1;

	int ret = -1;
	unsigned short tsw = 0;
	unsigned char rxbuf[1024] = {0};
	vla_data_t vla;
	unsigned char dataXX;
		
	vla.cmd = LNT_R_CMD;
	vla.cla = LNT_R_CLA;
	vla.ins = 0x93;
	vla.p1 = 0x00;   //0x01:加密
	vla.p2 = 0x00;
	vla.leorlc = 0x18; //加密数据

	__cla_var_data_hd(&vla);
	__cla_var_data_set(&dataXX, 1);
	__cla_var_data_crc();
	
	ret = lib_serial_send(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, sizeof(rxbuf), msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_R_CMD))
				return -1;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return -1;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			if(sw != NULL)
			{
				memcpy(&tsw, &(rxbuf[ret - 3]), 2);
				*sw = LNT_NTOHS(tsw);
			}
				
			memcpy(ack, &rxbuf[4], rxbuf[1] - 5);
			
			return ret;	
		}
	}

	return ret;		
}



















int dev_r_deposit_init(lnt_r_deposit_init_req_t *req, lnt_r_deposit_init_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if((req == NULL) ||(ack == NULL))
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len= LNT_PROT_RX_LEN + sizeof(lnt_r_deposit_init_ack_t); 
	
	__var_data_hd();
	__var_data_cmd(LNT_DEPOSIT_INIT);
	__var_data_set(req, sizeof(lnt_r_deposit_init_req_t));
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_DEPOSIT_INIT))
				return LNT_ERROR;

			rxlen = ret - 2;  
			if(rxlen != rxbuf[1])  
				return LNT_ERROR;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_r_deposit_init_ack_t));
			
			return ret;
		}
	}
	
	return LNT_ERROR;
	
}

int dev_r_deposit_process(lnt_r_deposit_process_req_t *req, lnt_r_deposit_process_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if((req == NULL) ||(ack == NULL))
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = LNT_PROT_RX_LEN + sizeof(lnt_r_deposit_process_ack_t); //期望返回的数据长度
	
	__var_data_hd();
	__var_data_cmd(LNT_DEPOSIT_PROCESS);
	__var_data_set(req, sizeof(lnt_r_deposit_process_req_t));
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_DEPOSIT_PROCESS))
				return LNT_ERROR;
			
			rxlen = ret - 2;  //实际返回的数据长度
			if(rxlen != rxbuf[1])  //判断岭南通应返回的数据长度和实际返回的数据长度是否一致
				return LNT_ERROR;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_r_deposit_process_ack_t));
			
			return ret;
		}
	}
	
	return LNT_ERROR;	
}

/* 
 * 发往读卡器的APDU指令	
 */
int dev_apdu_to_reader(lnt_r_apdu_req_t *req, lnt_reader_ack_t *ack, const unsigned int msec)
{
	if(req == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = sizeof(lnt_reader_ack_t);
	
	__var_data_hd();
	__var_data_cmd(LNT_READER_APDU);
	__var_data_set(req, req->len + 1);
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2((char *)__FUNCTION__, (unsigned char *)rxbuf, ret, 16);
		
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_READER_APDU))
				return LNT_ERROR;

			rxlen = ret - 2;  
			if(rxlen != rxbuf[1])
				return LNT_ERROR;

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;

			if(ack != NULL)
				memcpy(ack, &rxbuf, rxbuf[1] + 2);
			
			return ret;
		}
	}
	
	return LNT_ERROR;	
}


int dev_qry_admin_card_info(lnt_admin_card_info_req_t *req, lnt_admin_card_info_ack_t *ack, unsigned char *stat, const unsigned int msec)
{
	if((req == NULL) ||(ack == NULL))
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = sizeof(lnt_admin_card_info_ack_t) + LNT_PROT_RX_LEN;
		
	__var_data_hd();
	__var_data_cmd(LNT_QRY_RENT_INFO);
	__var_data_set(req, sizeof(lnt_admin_card_info_req_t));
	__var_data_crc();

	//__var_data_printf();
	
	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_QRY_RENT_INFO))
				return LNT_ERROR;

			rxlen = ret - 2;  
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;
			
			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			memcpy(ack, &rxbuf[4], sizeof(lnt_admin_card_info_ack_t));
			
			return ret;
		}
	}
	
	return LNT_ERROR;		
}

int dev_set_admin_card_info(lnt_admin_card_info_t *admin, unsigned char *stat, const unsigned int msec)
{
	if(admin == NULL)
		return -1;

	int ret = -1;
	unsigned char rxbuf[1024] = {0};
	unsigned char rxlen = 0;
	unsigned int expect_len = LNT_PROT_RX_LEN;
	
	__var_data_hd();
	__var_data_cmd(LNT_SET_RENT_INFO);
	__var_data_set(admin, sizeof(lnt_admin_card_info_t));
	__var_data_crc();

	//__var_data_printf();

	admin->admin_36bytes.A1 = 0x00;  //0x00:正常, 0x01:应用锁定,!!!!!谨慎写入！

	ret = lib_writen(g_serial.sfd, VAR_DATA, VAR_DATA_LEN);
	if(ret > 0)
	{
		ret = lib_serial_readn_select(g_serial.sfd, rxbuf, expect_len, msec);
		if(ret > 0)
		{
			//lib_printf_v2(__FUNCTION__, rxbuf, ret, 16);
			
			if((rxbuf[0] != LNT_RD_HEAD) || (rxbuf[2] != LNT_SET_RENT_INFO))
				return LNT_ERROR;

			rxlen = ret - 2;  
			if(rxlen != rxbuf[1]) 
				return LNT_ERROR;			

			if(__calc_check_sum(rxbuf, ret - 1) != rxbuf[ret - 1])
				return LNT_ERROR;
			
			if(stat != NULL)
				*stat = rxbuf[3];

			return ret;
		}
	}

	return LNT_ERROR;		
}


/* ----------------------------------------- 读卡器升级 ----------------------------------------- */
#if 1
#define	DEBUG_OUTPUT 0		//调试信息输出

#define	DEV_NAME 		 "/dev/ttyO1" //岭南通读卡器串口

#define LIB_UPDATE_OK			1	//升级成功
#define LIB_UPDATE_ERR   		2	//升级失败

#define SEND_MSLEEP		40	 //每发送一个数据包的时间间隔(ms),建议>=40!

#define	UPDATE_STAT 	0xF1 //升级状态设置
#define PRE_UPDATE		0xF2 //预升级			
#define	EXIT_UPDATE		0xF3 //退出升级状态

#define	MAX_SIZE		200*1024 //固件缓冲区大小
#define	SEND_SIZE		256  	 //升级时每次发送的数据包大小

unsigned char exit_update[16] = {0xFF,0x4C,0x4E,0x54,0xFF,0x46,0x49,\
	0x52,0x4D,0x57,0x41,0x52,0x45,0xFF,0x00,0xFF}; //退出升级模式
	

static volatile unsigned int update_stat = READER_UPDATE_FAILED;	//记录读卡器升级状态

/*获取读卡器升级的状态*/
enum UPDATE_STATUS lib_get_update_status(void)
{
	lib_gcc_atmoic_get(&update_stat);
	//printf("-----------reader update status:%d\n", update_stat);
	
	return update_stat;
}

int dev_reader_update(char *firmware_path)
{
		
		int gpio_ctrl_fd = -1;	//gpio
		int ret = -1, rret = -1, i = 0, flag = 0, result = LIB_UPDATE_ERR;
		time_t begin_times, end_times;
		unsigned int consume_time = 0;//升级耗时
		float update_times = 0, success_times = 0, failed_times = 0, exit_faild = 0;
		//升级次数		成功次数		失败次数		退出升级模式失败次数
		
		lib_gcc_atmoic_set(&update_stat,READER_UPDATE_ING);
		
		if(NULL == firmware_path)
		{
			return LIB_UPDATE_ERR;
		}
	
#if SUCCESS_RATE_TEST
	while(1)
#endif
	{  //循环进行固件升级并统计升级成功率
		
		//固件字符串分割,后面判断升级是否成功用
		char *buf = NULL, *pre_buf = NULL;
		const char *delim = "_";//分割符
	
		char firm_name[128] = {0}, firmware[128] = {0};
		//printf("--------%d, %s\n", strlen(firmware_path), firmware_path);
		memcpy(&firm_name,firmware_path,strlen(firmware_path));
		memcpy(&firmware,firmware_path,strlen(firmware_path));
	
		buf = strtok((char *)&firm_name,delim);
		while(NULL != buf){
			//printf("%s\n",buf);
			pre_buf = buf;//保存分割出的字符串,取最后一个分割出的字符串
			buf = strtok(NULL,delim);
		}
	
		flag = 0;//退出升级模式失败标记,成功为1
		update_times++;
		
		time(&begin_times);
		
		//------------读卡器固件升级---------------//
		//升级前读取固件
		unsigned char rxbuf[128] = {0}, success_recv[5] = {0}, status = -1;
		unsigned int fsize = -1;//不能是unsigned short!
		FILE *fp;
		
		fp = fopen(firmware_path,"r");
		if(NULL == fp){
			perror("fopen");
			goto err;
		}
		
#if 0
		fseek(fp, 0L, SEEK_END);  
		fsize = ftell(fp);
#else
		int type;
		struct stat statbuff;
		memset(&statbuff,0,sizeof(statbuff));
		
		if(stat(firmware_path, &statbuff) < 0){  
			goto exit;	
		}else{	
			fsize = statbuff.st_size; //文件大小
			type = statbuff.st_mode & S_IFREG;//文件类型
		}
#endif
	
		if(type != S_IFREG){	
		#if DEBUG_OUTPUT
			printf("file format error!\n"); 
		#endif
			goto exit;
		}
	#if DEBUG_OUTPUT
		printf("update firmware:%s\n",firmware);
		printf("file size = %d bytes.\n",fsize);//文件总大小
	#endif
		unsigned int size = -1, lsize = -1, pre_size = -1, packets = -1;
		unsigned char sbuf[MAX_SIZE] = {0};
	
		rewind(fp);
		size = fread(sbuf,fsize,1,fp);//读取整个文件	
	#if DEBUG_OUTPUT
		printf("read size = %d bytes.\n",size * fsize); 
	#endif
#if 1	
		//step1:退出升级模式
		for(i = 0; i < 3; i++)//尝试3次
		{
			ret = dev_CLA_INS_packet(&exit_update,sizeof(exit_update),&rxbuf,0,&status,EXIT_UPDATE,5000);
			if((ret <= 0) || (status != 0x00))
			{	
		#if DEBUG_OUTPUT
				fprintf(stderr,"exit_update failed! ret = %d, status = %02x\n",ret,status);
		#endif
				lib_sleep(1);
			}
			else{
				flag = 1;//成功标志
				break;
			}
		}
		if((flag == 0) && (i == 3))
		{
			failed_times++;
			exit_faild++;
			goto exit;
		}
		
		//step2:【升级状态】设置为升级模式开启
		update_stat_set_t set_stat;
		memset(&set_stat,0,sizeof(set_stat));
	
		lsize = fsize - 240;//剩下的字节
		packets = (lsize / 256) + ((lsize % 256)?1:0) + 1;//总数据包数(包括前240字节)
#if DEBUG_OUTPUT
		printf("total packets = %d.\n",packets);
#endif
		set_stat.mode = 0x01;//升级模式开启
		set_stat.total_packets = (unsigned short)packets;
		
		ret = dev_CLA_INS_packet(&set_stat,sizeof(set_stat),&rxbuf,0,&status,UPDATE_STAT,5000);//延时建议>=1600
		if((ret <= 0) || (status != 0x00))
		{
	#if DEBUG_OUTPUT
			fprintf(stderr,"update_stat_set start failed! ret = %d, status = 0x%02x\n",ret,status);
	#endif
			failed_times++;
	#if 1
			//升级失败后退出升级模式
			for(i = 0; i < 3; i++)//尝试3次
			{
				ret = dev_CLA_INS_packet(&exit_update,sizeof(exit_update),&rxbuf,0,&status,EXIT_UPDATE,500);
				if((ret <= 0) || (status != 0x00))
				{
			#if DEBUG_OUTPUT
					fprintf(stderr,"exit_update failed! ret = %d, status = %02x\n",ret,status);
			#endif
					lib_sleep(1);
				}
				else{
					flag = 1;//成功标志
					break;
				}
			}
			if((flag == 0) && (i == 3))
			{
				failed_times++;
				exit_faild++;
				goto exit;
			}
	#endif
			goto exit;
		}
		
		//step3:预升级(发送固件前面240个字节)
		unsigned int send_size = -1;
		pre_update_ack_t pre_ack;
		memset(&pre_ack,0,sizeof(pre_ack));
		
		ret = dev_CLA_INS_packet(&sbuf,240,&pre_ack,sizeof(pre_ack),&status,PRE_UPDATE,500);
		if((ret <= 0) || (status != 0x00))
		{
	#if DEBUG_OUTPUT
			fprintf(stderr,"pre_update failed! ret = %d, status = 0x%02x\n",ret,status);
	#endif
			failed_times++;
	#if 1
			//升级失败后退出升级模式
			for(i = 0; i < 3; i++)//尝试3次
			{
				ret = dev_CLA_INS_packet(&exit_update,sizeof(exit_update),&rxbuf,0,&status,EXIT_UPDATE,500);
				if((ret <= 0) || (status != 0x00))
				{
			#if DEBUG_OUTPUT
					fprintf(stderr,"exit_update failed! ret = %d, status = %02x\n",ret,status);
			#endif
					lib_sleep(1);
				}
				else{
					flag = 1;//成功标志
					break;
				}
			}
			if((flag == 0) && (i == 3))
			{
				failed_times++;
				exit_faild++;
				goto exit;
			}
	#endif
			goto exit;
		}
	#if DEBUG_OUTPUT
		lib_printf_v2("pre_update ack",(unsigned char *)&pre_ack,sizeof(pre_ack),16);
	#endif
 #if 1
		//step4:升级(发送剩下的数据包)
		unsigned int index, len = 0;
	 
		fseek(fp, pre_ack.next_position, SEEK_SET);//跳转到预升级返回的指定长度 
		pre_size = ftell(fp);
		send_size = pre_size;
	#if DEBUG_OUTPUT
		printf("pre_ack, position = %d.\n",send_size);
	#endif
		lsize = fsize - pre_size;//预升级后剩下的字节
		packets = (lsize / 256) + ((lsize % 256)?1:0) + 1;//总数据包数
#if DEBUG_OUTPUT
		printf("left size = %d bytes.\n",lsize); 
		printf("total packets = %d.\n",packets);
		
		printf("already send size:%d, file size:%d\n",send_size,fsize);
#endif
#if 1
		while(send_size < fsize){
			for(index = 1; index < packets; index++){
				if(index < packets - 1){
					len = SEND_SIZE;//中间每个数据包大小
				}else{
					len = fsize - pre_size - ((index - 1) * SEND_SIZE);//最后一个数据包大小
				}
		#if DEBUG_OUTPUT
				printf("---------->index = %d\n",index);
				printf("---------->packet size = %d\n",len);
		#endif
			
				ret = lib_serial_send(g_serial.sfd,&sbuf[send_size],len);
				//ret = lib_serial_writen_select(g_serial.sfd,&sbuf[send_size],len,2000);
		#if DEBUG_OUTPUT
				//printf("			   send size = %d\n",ret);
		#endif
				lib_msleep(SEND_MSLEEP);//发送太快的话升级不能成功!!
				//每发送完一个数据包后读取发送状态,读取读卡器响应才能确保升级成功!
				rret = lib_serial_recv(g_serial.sfd,&rxbuf,5);
				//rret = lib_serial_readn_select(g_serial.sfd,&rxbuf,5,2000);
		#if DEBUG_OUTPUT			
				lib_printf_v2("--------recv--------",(unsigned char *)&rxbuf,rret,16);
		#endif			
				if((ret != len) || ((memcmp((char *)&rxbuf, (char *)&success_recv, 5)) != 0))
				{
			#if DEBUG_OUTPUT
					fprintf(stderr,"send packet error! index = %d, packet len:%d, send len:%d\n",index,len,ret);
			#endif
					failed_times++;
	
				#if 1
						//升级失败后退出升级模式
						for(i = 0; i < 3; i++)//尝试3次
						{
							ret = dev_CLA_INS_packet(&exit_update,sizeof(exit_update),&rxbuf,0,&status,EXIT_UPDATE,500);
							if((ret <= 0) || (status != 0x00))
							{
						#if DEBUG_OUTPUT
								fprintf(stderr,"exit_update failed! ret = %d, status = %02x\n",ret,status);
						#endif
								lib_sleep(1);
							}
							else{
								flag = 1;//成功标志
								break;
							}
						}
						if((flag == 0) && (i == 3))
						{
							failed_times++;
							exit_faild++;
							goto exit;
						}
				#endif
					result = LIB_UPDATE_ERR;
					goto exit;
				}
	
				//至此才发送成功
				send_size += ret;	//已发送数据包大小
		#if DEBUG_OUTPUT
				printf("already send [%d] bytes.\n\n",send_size);
		#endif
			}
		}
#if DEBUG_OUTPUT
		printf("\nupdate firmware:%s\n",firmware);
		printf("file size = %d bytes.\n",fsize);//文件总大小
		printf("total packets = %d.\n",packets);
		printf("first packet size = %d.\n",pre_size);
	
		if(send_size == fsize){
			printf("\nsend packet complete!\n");
		}
#endif
#endif
	
		//step5:【升级状态】设置为升级完成
		memset(&set_stat,0,sizeof(set_stat));
		set_stat.mode = 0x02;//升级完成，待校验数据
	
		//读卡器不会有回应,但必须执行这一步
		ret = dev_CLA_INS_packet(&set_stat.mode,sizeof(set_stat.mode),&rxbuf,0,&status,UPDATE_STAT,1000);
	
		lib_sleep(8);//等待读卡器重启
	
	
		//读取固件版本并据此判断升级是否成功
#if 1
		//岭南通模块串口初始化
		lib_lnt_config_t config;
		memset(&config,0,sizeof(lib_lnt_config_t));
		
		strcpy((char *)config.tty, DEV_NAME);
		config.baudrate = 115200;
		ret = lnt_init(&config);
		if(ret < 0){
		#if DEBUG_OUTPUT
			fprintf(stderr,"lnt_init failed,ret = %d\n",ret);
		#endif
			failed_times++;
			goto exit;
		}
	#if DEBUG_OUTPUT
		fprintf(stderr,"lnt_init success!\n");
	#endif
#endif
		lnt_getversion_ack_t version;
		memset(&version, 0, sizeof(lnt_getversion_ack_t));
		
		dev_get_version(&version,&status,100);
	#if DEBUG_OUTPUT
		fprintf(stderr,"dev_get_version,stat = 0x%02x\n",status);
		fprintf(stderr,"software version:%s\n",version.version); 
	#endif
	
		
		printf("update ver:%s\n",pre_buf);
#if 1
		if(!strncmp((char *)&version,pre_buf,8)){
		#if 1 //DEBUG_OUTPUT
			printf("\n恭喜您,读卡器固件升级成功啦!\n");
		#endif
			
			lib_gcc_atmoic_set(&update_stat,READER_UPDATE_SUCCESS);
			
			success_times++;
			result = LIB_UPDATE_OK;
#if SUCCESS_RATE_TEST
			printf("pre_buf len:%d\n",strlen("20160219"));
			if(strncmp(pre_buf, "20160219", strlen("20160219"))){	//该次升级版本不是20160219
				firmware_path = FIRMWARE;
			}
			else{
				firmware_path = FIRMWARE1;
			}
#endif
		}else{
		#if 1 //DEBUG_OUTPUT
			printf("\n抱歉,读卡器固件升级失败啦!\n");
		#endif
	
			lib_gcc_atmoic_set(&update_stat,READER_UPDATE_FAILED);
	
			failed_times++;
			result = LIB_UPDATE_ERR;
			#if 0
			gpio_ctrl_fd =  open("/dev/gpio_ctrl", O_RDWR);
			printf("----------gpio_ctrl_fd:%d\n", gpio_ctrl_fd);
			ioctl(gpio_ctrl_fd, GPIO_CTRL_SET_PWR_YCT, GPIO_CTRL_SW_OFF); 	//岭南通读卡器上电
			ioctl(gpio_ctrl_fd, GPIO_CTRL_SET_PWR_YCT, GPIO_CTRL_SW_ON); 	//岭南通读卡器上电
			#endif
			lnt_init(&config);
		}
#endif
		time(&end_times);
		consume_time = end_times - begin_times;
	#if DEBUG_OUTPUT
		printf("Update takes %d seconds.\n",consume_time);//升级耗时
	#endif
	
#endif
#endif
	
	exit:
		if(fp != NULL)
			ret = fclose(fp);
		if(ret == EOF){
			perror("fclose");
		}
#if DEBUG_OUTPUT
		printf("\n######################################################################################\n");
		printf("\n\tUpdate times:%.0f, Success times:%.0f, Failed times:%.0f, Take time:%ds.\n",update_times,success_times,failed_times,consume_time);
		printf("\tSuccess percent:%5.2f%%\n",100*success_times/update_times);	
		printf("\n######################################################################################\n");	
#endif
	}//while(1)
	
	err:
		//lib_lnt_release(); //不用!
		//lib_close(gpio_ctrl_fd);
	
		return result;
}
#endif




























