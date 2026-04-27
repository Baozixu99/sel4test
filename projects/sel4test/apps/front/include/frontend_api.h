#ifndef FRONTEND_API_H_
#define FRONTEND_API_H_

#include "engine.h"
#include "session.h"
#include "session_pool.h"
#include "message.h"
#include "frontend_proto.h"
#include "common_utils.h"


int frontend_eng_command(struct FrontendEngine_ *eng, GeneralProxyMsgHeader *hdr, uint8_t *data, uint32_t size);

struct FrontendSession *frontend_sess_new(struct FrontendEngine_ *eng);
int frontend_sess_connect_by_addrstr(struct FrontendSession *sess, int proto, const char *addr_str);
int frontend_sess_connect_by_addrstr_devid(struct FrontendSession *sess, int proto, const char *addr_str, uint16_t dev_id);

int frontend_sess_connect(struct FrontendSession *sess, struct SessMsgPara *para);
int frontend_sess_close(struct FrontendSession *sess);
int frontend_sess_send(struct FrontendSession *sess, uint8_t *data, uint32_t size);
int frontend_sess_recv(struct FrontendSession *sess, uint8_t *data, uint32_t size);
int frontend_sess_bind_callback(struct FrontendSession *sess, SESS_EVENT_CALLBACK event_callback);


int frontend_iot_sess_send(IoTFrontendSession *sess, uint8_t *data, uint32_t size);
int frontend_iot_sess_recv(IoTFrontendSession *sess, uint8_t *data, uint32_t size);


#endif