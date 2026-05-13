/*
 * Monkey Network Protocol
 *
 * Created on 2024.12 at Minhang, Shanghai
 * 
 * 
 * feng.yt [at]  sjtu  [dot] edu [dot] cn
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#pragma once

#include <adl/stdint.h>

namespace monkey::net::protocol {
    const adl::int32_t LATEST_VERSION = 2;
}

#include <monkey/net/protocol/defines.h>

#include <monkey/net/protocol/Protocol1Connection.h>
#include <monkey/net/protocol/Protocol2Connection.h>
