#ifndef __DEVICE_H__
#define __DEVICE_H__

#include "lnt.h"

int dev_init(char *tty, const unsigned int baudrate);
void dev_release(void);

int dev_get_version(lnt_getversion_ack_t *ack, unsigned char *stat, const unsigned int msec);
int dev_core_init(lnt_core_init_req_t *req, lnt_core_init_ack_t *ack, unsigned char *stat, const unsigned int msec);
int dev_set_parameter(lnt_parameter_set_req_t *req, unsigned char *stat, const unsigned int msec);
int dev_qry_ticket_info(lnt_qry_ticket_info_ack_t *ack, unsigned char *stat, const unsigned int msec);
int dev_qry_rent_info(lnt_rent_info_req_t *req, lnt_rent_info_ack_t *ack, unsigned char *stat, const unsigned int msec);
int dev_set_rent_info(lnt_rent_info_t *rent, unsigned char *stat, const unsigned int msec);
int dev_set_blacklist_flag(lnt_blacklist_record_ack_t *ack, unsigned char *stat, const unsigned int msec);
int dev_get_history_record(lnt_history_record_ack_t *ack, unsigned char *stat, const unsigned int msec);
int dev_get_trade_record(lnt_trade_record_ack_t *ack, unsigned char *stat, const unsigned int msec);
int dev_consume(lnt_consume_req_t *req, unsigned char *stat, const unsigned int msec);



int dev_CLA_INS_pkg(void *s_ptr, const unsigned int s_len, void *d_ptr, const unsigned int d_len, unsigned char *d_stat, const unsigned int msec);


int dev_r_init(lnt_r_init_ack_t *ack, unsigned char *stat, const unsigned int msec); //读卡器初始化
int dev_r_ac_login_1(lnt_r_ac_login_1_ack_t *ack, unsigned char *stat, const unsigned int msec);	//签到步骤1
int dev_r_ac_login_2(lnt_r_ac_login_2_req_t *req, lnt_r_ac_login_2_ack_t *ack, unsigned char *stat, unsigned short *sw, const unsigned int msec);  //签到步骤2
int dev_r_pub_qry_his(lnt_r_pub_qry_his_ack_t *ack, unsigned char *stat, unsigned short *sw, const unsigned int msec);  //查询卡交易信息
int dev_r_pub_qry_physics_info_NoEncry(lnt_r_pub_qry_physics_info_NoEncry_ack_t *ack, unsigned char *stat, unsigned short *sw, const unsigned int msec);  //查询卡物理信息(加密)
int dev_r_pub_qry_physics_info_Encry(lnt_r_pub_qry_physics_info_Encry_ack_t *ack, unsigned char *stat, unsigned short *sw, const unsigned int msec); //查询卡物理信息(不加密)


int dev_r_deposit_init(lnt_r_deposit_init_req_t *req, lnt_r_deposit_init_ack_t *ack, unsigned char *stat, const unsigned int msec);   //押金初始化
int dev_r_deposit_process(lnt_r_deposit_process_req_t *req, lnt_r_deposit_process_ack_t *ack, unsigned char *stat, const unsigned int msec);  //押金处理

int dev_apdu_to_reader(lnt_r_apdu_req_t *req, lnt_reader_ack_t *ack, const unsigned int msec);  




int dev_qry_admin_card_info(lnt_admin_card_info_req_t *req, lnt_admin_card_info_ack_t *ack, unsigned char *stat, const unsigned int msec);
int dev_set_admin_card_info(lnt_admin_card_info_t *admin, unsigned char *stat, const unsigned int msec);



int dev_reader_update(char *firmware_path);







#endif

