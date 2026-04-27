#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <arch_stdio.h>
#include <platsupport/delay.h>

#include "frontend_api.h"
#include "senario_test.h"


void test_session_event_callback(struct FrontendSession* sess, FrontendSessionEvent event);

extern void tailq_test();
extern void uthash_test();



/**
 * @brief Test event callback function for frontend sessions
 *
 * This is the test-oriented callback implementation for handling frontend session events,
 * which is invoked by the session framework when specific events (defined in 
 * FrontendSessionEvent enumeration) occur during the lifecycle of a FrontendSession.
 * It is designed exclusively for testing and validation purposes (e.g., unit testing, 
 * integration testing of the session framework), and provides test-specific event processing 
 * logic instead of production-grade business handling.
 *
 * @param[in] sess Pointer to the FrontendSession instance that triggered the event;
 *                 must be a valid non-NULL pointer pointing to an active session
 * @param[in] event The specific event type that occurred, one of the values in 
 *                  FrontendSessionEvent enumeration (FRONTEND_SESS_EVENT_CONN, 
 *                  FRONTEND_SESS_EVENT_RECVDATA, FRONTEND_SESS_EVENT_TIMEOUT, FRONTEND_SESS_EVENT_CLOSE)
 *
 * @note 1. This callback is called automatically by the session management framework,
 *          and should not be invoked manually by the user;
 *       2. The test implementation typically includes operations such as 
 *          event logging for test verification, capturing event occurrence status, 
 *          simulating edge-case processing (e.g., intentionally triggering error paths on FRONTEND_SESS_EVENT_TIMEOUT,
 *          validating data integrity on FRONTEND_SESS_EVENT_RECVDATA, or checking resource cleanup on FRONTEND_SESS_EVENT_CLOSE);
 *       3. This callback is NOT intended for production use — it should only be used in 
 *          test environments to validate the correctness of session event triggering and handling;
 *       4. Ensure the 'sess' pointer is valid when the callback is triggered (the framework
 *          guarantees this under normal circumstances), invalid pointers may lead to undefined behavior;
 *       5. The processing logic in this callback should be non-blocking to avoid blocking the
 *          session framework's event loop, even in test scenarios.
 */
void test_session_event_callback(struct FrontendSession* sess, FrontendSessionEvent event){
    uint8_t snd_buf[4096],  rcv_buf[4096];
    int ret;
    
    if(FRONTEND_SESS_EVENT_CONN == event){
        utils_print("In %s: FRONTEND_SESS_EVENT_CONN triggered!\n", __func__);
        utils_print("%s: Starting to send test data...\n", __func__);
        memset(snd_buf, 0, sizeof(snd_buf));
        snprintf((char*)snd_buf, sizeof(snd_buf), "test msg");  
        ret = frontend_sess_send(sess, snd_buf, strlen((char*)snd_buf)); 
        utils_print("%s: frontend_sess_send returned %d bytes (sent data: \"test msg\")\n", __func__, ret);
    }else if(FRONTEND_SESS_EVENT_RECVDATA == event){
        utils_print("In %s: FRONTEND_SESS_EVENT_RECVDATA triggered!\n", __func__);
        ret = frontend_sess_recv(sess, rcv_buf, sizeof(rcv_buf));
        utils_print("%s: frontend_sess_recv received %d bytes of data\n", __func__, ret);
        frontend_sess_send(sess, rcv_buf, ret);
    }else if(FRONTEND_SESS_EVENT_CLOSE == event){
        utils_print("In %s: FRONTEND_SESS_EVENT_CLOSE triggered!\n", __func__);
    }else if(FRONTEND_SESS_EVENT_ABNORMAL == event){
        utils_print("In %s: FRONTEND_SESS_EVENT_ABNORMAL triggered!\n", __func__);
    }

    return;
}


int main(void){
    FrontendEngine          *eng;
    struct FrontendSession  *sess;
    ProxyMsgHeader          *proxy_msg_hdr;
    IotModbusTcpMsg         modbus_tcp_msg;
    int                     ret;
    size_t                  block_size;
    uint8_t                 msg_buf[HYPERAMP_MSG_HDR_PLUS_MAX_SIZE];

    memset(&modbus_tcp_msg, 0, sizeof(modbus_tcp_msg));
    modbus_tcp_msg.cmd = IOT_MODBUS_CMD_READ_REQ;// Read reg command

    frontend_engine_init();

    eng  = frontend_get_global_engine();

//    test_proxy_scenario_multi_type_msg_build_frontend_hyperamp(eng);
#if 1
    sess    = frontend_sess_new(eng);
    ret     = frontend_sess_connect_by_addrstr(sess, SESS_UDP_PROTO, "192.168.137.2:8888");
    frontend_sess_bind_callback(sess, test_session_event_callback);

    frontend_iot_sess_send(frontend_bluetooth_sess, "test bluetooth", strlen("test bluetooth"));
    frontend_iot_sess_send(frontend_can_sess, "Hello", strlen("Hello"));
    frontend_iot_sess_send(frontend_modbustcp_sess, &modbus_tcp_msg, sizeof(modbus_tcp_msg));
#endif



#if 0    
    int cnt = 0;
    while(1){
//        sleep(2);
        ps_sdelay(2);
        
        ret = frontend_engine_hyperamp_rx_queue_get(eng, HYPERAMP_MSG_HDR_PLUS_MAX_SIZE, msg_buf, &block_size);

        if(FRONTEND_PROXY_PROCESS_OK == ret){
            proxy_msg_hdr = (ProxyMsgHeader *)msg_buf;
            utils_print("version = %d, msg type = %d, frontend ID = %d, backend ID = %d, msg len = %d\n", 
                         proxy_msg_hdr->version, proxy_msg_hdr->proxy_msg_type, proxy_msg_hdr->frontend_sess_id, proxy_msg_hdr->backend_sess_id, proxy_msg_hdr->payload_len);
            
            utils_print("Before enter frontend_proxy_msg_process, the content of the proxy message is:\n");
            DUMP_BUFFER_CONTENT(msg_buf, proxy_msg_hdr->payload_len + sizeof(ProxyMsgHeader), "%02x ");

            DUMP_PROXY_MSG_HEADER(msg_buf);
            frontend_proxy_msg_process(msg_buf);

        }else if(FRONTEND_PROXY_PROCESS_AGAIN == ret){
            utils_print("In frontend, HyperAMP RX queue is empty!\n");
        }else{
            utils_print("Failed to get message in HyperAMP RX queue!\n");
        }

        test_frontend_proxy_scenario_process_active_b2f_sess_queue(eng);
        test_frontend_proxy_scenario_process_active_f2b_sess_queue(eng);

        if(cnt > 50){
            break;
        }

        data_msg_inject_frontend_hyperamp(eng);
        
        cnt++;
    }
#endif

    frontend_engine_run_hyperamp();

    return 0;
}
