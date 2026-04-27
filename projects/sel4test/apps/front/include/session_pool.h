#ifndef SESSION_POOL_H
#define SESSION_POOL_H

#include <stdint.h> 
#include <queue.h>

#include "message.h"
#include "session.h"

struct FrontendEngine_;

struct FrontendSessionPoolOps;

struct FrontendSessionPool {
    char                            *pool_name;     // Name/identifier of the session pool
    int                             sess_num;       // Current number of sessions in the pool
    int                             capacity;       // Maximum number of sessions the pool can hold (total capacity)
    struct FrontendEngine_           *engine;        // Pointer to the engine this pool belongs to
    struct FrontendSessionPoolOps    *ops;           // Set of operation functions for the pool (e.g., create, delete)
    struct FrontendSessionQueue      queue_f2b;      // Queue containing active sessions
    struct FrontendSessionQueue      queue_b2f;      // Queue for backend-to-frontend session communication/mapping
    struct FrontendSessionIDQueue    id_queue;       // Queue holding available/reusable session IDs
    struct FrontendSession           *htable;        // Hash table storing sessions (for efficient lookup by ID)
};




struct FrontendSessionPoolOps {
    int (*create_sess_step1)(struct FrontendSessionPool *s_pool, struct FrontendSession *sess,  struct SessMsgPara *para);
    int (*create_sess_step2)(struct FrontendSessionPool *s_pool, struct FrontendSession *sess,  uint16_t sess_id, SessOpRespData *resp);
    int (*create_sess_passive)(struct FrontendSessionPool *s_pool, struct FrontendSession *sess, struct SessMsgPara *para);
    int (*insert_sess)(struct FrontendSessionPool *s_pool, struct FrontendSession *sess);
    struct FrontendSession* (*search_sess)(struct FrontendSessionPool *s_pool, uint16_t id);
    int (*delete_sess)(struct FrontendSessionPool *s_pool, struct FrontendSession *sess);
    int (*data_process)(struct FrontendSession *sess);
    int (*data_process_b2f)(struct FrontendSession *sess);
    int (*data_process_f2b)(struct FrontendSession *sess);
    int (*close_sess_step1)(struct FrontendSessionPool *s_pool, struct FrontendSession *sess);
    int (*close_sess_step2)(struct FrontendSessionPool *s_pool, struct FrontendSession *sess,  SessOpRespData *resp);
    void (*destroy_pool)(struct FrontendSessionPool *s_pool);
};

extern struct FrontendSessionPool  *front_high_speed_pool;
int frontend_high_speed_init_pool(struct FrontendSessionPool *pool); 

struct FrontendSessionPool *frontend_get_high_speed_pool();

//helper func
uint16_t allocate_id(struct FrontendSessionIDQueue *id_q);
void release_id(struct FrontendSessionIDQueue *id_q, uint16_t id);
void print_pool(struct FrontendSessionPool *s_pool);
void high_speed_delete_all_sess(struct FrontendSessionPool *s_pool);
void fill_id_queue(struct FrontendSessionIDQueue *id_q);
void inc_sess_num(struct FrontendSessionPool *pool);
void dec_sess_num(struct FrontendSessionPool *pool);


int frontend_high_speed_data_process_b2f(struct FrontendSession *sess);
int frontend_high_speed_data_process_f2b(struct FrontendSession *sess);


#endif