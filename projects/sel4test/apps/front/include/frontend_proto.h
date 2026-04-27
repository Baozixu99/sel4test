#ifndef FRONTEND_PROTO_H_
#define FRONTEND_PROTO_H_

#include "message.h"
#include "session.h"
#include "session_pool.h"
#include "common_utils.h"


#define PROXY_MSG_TYPE_VALID(x) (((x) == PROXY_MSG_TYPE_DEV)   || \
                                 ((x) == PROXY_MSG_TYPE_STRGY) || \
                                 ((x) == PROXY_MSG_TYPE_SESS)  || \
                                 ((x) == PROXY_MSG_TYPE_DATA)  || \
                                 ((x) == PROXY_MSG_TYPE_IOT)) 

#define PROXY_MSG_LEN_VALID(x) (((x) >= PROXY_MSG_MIN_SIZE)   || \
                                 ((x) <= PROXY_MSG_MAX_SIZE))

#define PROXY_ADMIN_SESSION_ID                               0
#define FRONTEND_ADMIN_SESSION_ID                            PROXY_ADMIN_SESSION_ID
#define BACKEND_ADMIN_SESSION_ID                             PROXY_ADMIN_SESSION_ID

#define PROXY_HANDOVER_SESSION_ID                            0xFFFF
#define FRONTEND_HANDOVER_SESSION_ID                         PROXY_HANDOVER_SESSION_ID
#define BACKEND_HANDOVER_SESSION_ID                          PROXY_HANDOVER_SESSION_ID

#define APP_SESSION_ID_VALID(x) (((x) != PROXY_ADMIN_SESSION_ID)   || \
                                 ((x) != PROXY_HANDOVER_SESSION_ID))

#define DEV_ID_AUTO_HANDOVER                                0xFF


int frontend_proxy_msg_process(uint8_t *msg);

int frontend_proxy_dev_msg_command(uint8_t *msg);
int frontend_proxy_dev_msg_process(uint8_t *msg);
int frontend_proxy_dev_msg_process_ver1(uint16_t msg_type, uint16_t msg_id, uint16_t action_type, uint16_t payload_len, uint8_t *msg_payload);
int frontend_proxy_dev_msg_process_disable_ver1(uint16_t payload_len, uint8_t *msg_payload);
int frontend_proxy_dev_msg_process_enable_ver1(uint16_t payload_len, uint8_t *msg_payload);
int frontend_proxy_dev_msg_process_query_ver1(uint16_t payload_len, uint8_t *msg_payload);

int frontend_proxy_strgy_msg_process(uint8_t *msg);
int frontend_proxy_strgy_msg_response(uint8_t *msg);
int frontend_proxy_strgy_msg_process_ver1(uint16_t msg_type, uint16_t msg_id, uint16_t action_type, uint16_t payload_len, uint8_t *msg_payload);
int frontend_proxy_strgy_msg_process_set_ver1(uint16_t payload_len, uint8_t *msg_payload);
int frontend_proxy_strgy_msg_process_query_ver1(uint16_t payload_len, uint8_t *msg_payload);



int frontend_proxy_sess_msg_process(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint8_t *msg);
int frontend_proxy_sess_msg_response(uint8_t *msg);
int frontend_proxy_sess_msg_process_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t msg_type, 
                                        uint16_t action_type, uint16_t ip_version, uint16_t payload_len, 
                                        uint8_t *msg_payload);


int frontend_proxy_sess_msg_process_active_create_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t ip_version, uint16_t payload_len, uint8_t *msg_payload);
int frontend_proxy_sess_msg_process_close_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t payload_len, uint8_t *msg_payload);


int __frontend_proxy_sess_msg_process_active_create_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t ip_version, uint16_t payload_len, uint8_t *msg_payload);
int __frontend_proxy_sess_msg_process_close_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t payload_len, uint8_t *msg_payload);


int frontend_proxy_sess_msg_process_passive_create_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t ip_version, uint16_t payload_len, uint8_t *msg_payload);
int __frontend_proxy_sess_msg_process_passive_create_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t ip_version, uint16_t payload_len, uint8_t *msg_payload);



int frontend_proxy_data_msg_prosess(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t data_len, uint8_t *msg);
int frontend_proxy_data_msg_recv(struct FrontendSession *sess, uint8_t *msg);
int frontend_proxy_data_msg_send(struct FrontendSession *sess, uint8_t *msg);



int frontend_proxy_shmem_data_msg_recv(struct FrontendSession *sess, uint8_t **msg);
int frontend_proxy_shmem_data_msg_send(struct FrontendSession *sess, const uint8_t **msg);
int frontend_proxy_sock_data_msg_recv(struct FrontendSession *sess, uint8_t *msg);
int frontend_proxy_sock_data_msg_send(struct FrontendSession *sess, uint8_t *msg);



int frontend_proxy_iot_msg_process(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t msg_len, uint8_t *msg);


int frontend_proxy_bluetooth_msg_process(uint16_t frontend_sess_id, 
                                         uint16_t backend_sess_id,
                                         IotMsgHeader *iot_header,
                                         uint8_t *iot_data);


int frontend_proxy_can_msg_process(uint16_t frontend_sess_id, 
                                   uint16_t backend_sess_id,
                                   IotMsgHeader *iot_header,
                                   uint8_t *iot_data);


int frontend_proxy_zigbee_msg_process(uint16_t frontend_sess_id, 
                                     uint16_t backend_sess_id,
                                     IotMsgHeader *iot_header,
                                     uint8_t *iot_data);


int frontend_proxy_lora_msg_process(uint16_t frontend_sess_id, 
                                    uint16_t backend_sess_id,
                                    IotMsgHeader *iot_header,
                                    uint8_t *iot_data);


int frontend_proxy_powerlink_msg_process(uint16_t frontend_sess_id, 
                                         uint16_t backend_sess_id,
                                         IotMsgHeader *iot_header,
                                         uint8_t *iot_data);

int frontend_proxy_modbustcp_msg_process(uint16_t frontend_sess_id, 
                                         uint16_t backend_sess_id,
                                         IotMsgHeader *iot_header,
                                         uint8_t *iot_data);
                                         
size_t get_iot_addr_length(IotProtoType addr_type);
#endif