/*

    monkey logging

    by gty
    created on 2025.10.30, Shanghai
*/

#pragma once

#include <base/log.h>
#include "./config.h"

#define MONKEY_LOG_INFO(...)   Genode::log("[MONKEY][INFO] ", __VA_ARGS__)
#define MONKEY_LOG_WARN(...)   Genode::warning("[MONKEY][WARN] ", __VA_ARGS__)
#define MONKEY_LOG_ERROR(...)  Genode::error("[MONKEY][ERROR] ", __VA_ARGS__)

#if MONKEY_CFG_ENABLE_DEBUG_LOGS
    #define MONKEY_LOG_DEBUG(...)  Genode::log("[MONKEY][DEBUG] ", __VA_ARGS__)
#else
    #define MONKEY_LOG_DEBUG(...)  do { } while (0)
#endif
