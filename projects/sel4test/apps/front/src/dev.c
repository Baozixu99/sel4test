#include "common_utils.h"
#include "dev.h"
#include "engine.h"
#include "session.h"
#include "session_pool.h"



FrontendDevInfo dev_array[] = {
    [0] = {
        .dev_status = 0,
        .dev_id = 0,
        .dev_type = 1,
        .name = "Frontend_Dev0"
    }
};

FrontendDevListCfg *p_global_dev_list_cfg = NULL;
FrontendDevListCfg global_dev_list_cfg;


void frontend_init_dev_list(){
    p_global_dev_list_cfg = &global_dev_list_cfg;
    memset(p_global_dev_list_cfg, 0, sizeof(p_global_dev_list_cfg));
    p_global_dev_list_cfg->dev_info = dev_array;
    p_global_dev_list_cfg->dev_num  = sizeof(dev_array)/sizeof(FrontendDevInfo);

    utils_print("In %s: number of device is %d\n", __func__, sizeof(dev_array)/sizeof(FrontendDevInfo));
}