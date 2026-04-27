#ifndef SENARIO_TEST_H_
#define SENARIO_TEST_H_

#include "engine.h"
#include "frontend_proto.h"
#include "message.h"
#include "shared_mem_io.h"


int scenario_msg_inject_frontend(FrontendEngine *engine,
                                GeneralProxyMsgHeader *msg_header,
                                const uint8_t *msg_payload,
                                size_t msg_payload_len,
                                MemoryAllocMode alloc_mode,
                                uint8_t **result_msg,
                                char *result_desc,
                                size_t desc_len);


int test_proxy_scenario_multi_type_msg_build_frontend(FrontendEngine *engine);



int device_msg_inject_frontend(FrontendEngine *engine);

int strategy_msg_inject_frontend(FrontendEngine *engine);

int session_msg_inject_frontend(FrontendEngine *engine);

int data_msg_inject_frontend(FrontendEngine *engine);

int test_proxy_scenario_msg_read_from_rx_queue_frontend(FrontendEngine *engine);


int test_proxy_scenario_multi_type_msg_build_frontend_hyperamp(FrontendEngine *engine);
int device_msg_inject_frontend_hyperamp(FrontendEngine *engine);
int strategy_msg_inject_frontend_hyperamp(FrontendEngine *engine);
int session_msg_inject_frontend_hyperamp(FrontendEngine *engine);
int data_msg_inject_frontend_hyperamp(FrontendEngine *engine);


int test_frontend_proxy_scenario_process_active_f2b_sess_queue(FrontendEngine *engine);

int test_frontend_proxy_scenario_process_active_b2f_sess_queue(FrontendEngine *engine);

#endif