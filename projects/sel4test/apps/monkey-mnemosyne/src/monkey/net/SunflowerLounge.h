/*
 * Sunflower Lounge : Base type for various lounges like App Lounge and Memory Lounge.
 *
 * Using lounge, you can let clients access only specified APIs.
 *
 * You should ensure client is authenticated before invited to the lounge.
 *
 * The name `Sunflower` is inspired by CMB.
 *   https://www.cmbchina.com/personal/allinonecard/cardinfo.aspx?guid=be48a9ab-300f-4393-b80e-02308ddf1a7f
 * 
 * gongty [at] tongji [dot] edu [dot] cn
 * Created on 2025.1.25 at Xiangzhou, Zhuhai, Guangdong
 */

#pragma once

#include <monkey/net/protocol.h>

namespace monkey::net {

template <typename ContextType, typename ProtocolConnectionType>
struct SunflowerLounge {

    ContextType& context;
    ProtocolConnectionType client;

    SunflowerLounge(
        ContextType& context,
        ProtocolConnectionType& client
    )
    : context {context}, client {client}
    {}

    virtual ~SunflowerLounge() {}

    virtual monkey::Status serve() = 0;
};


}  // namespace monkey::net

