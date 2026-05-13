/*
    Monkey Status

    Created on 2025.1.5

    gongty [at] tongji [dot] edu [dot] cn

*/

#pragma once

#include <adl/stdint.h>

namespace monkey {

enum class Status : adl::int32_t {
    SUCCESS = 0,
    OUT_OF_RESOURCE,
    NETWORK_ERROR,
    PROTOCOL_ERROR,
    INVALID_PARAMETERS,
    NOT_FOUND,
    NOT_INITIALIZED,
    FAILED
};

}


