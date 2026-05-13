/*
    TCP/IPv4 Socket

    created on 2025.2.5 at Xiangzhou, Zhuhai, Guangdong

    gongty [at] tongji [dot] edu [dot] cn
*/

#pragma once

#include <monkey/Status.h>
#include <monkey/net/IP4Addr.h>
#include <monkey/net/TcpIo.h>

#include <netinet/in.h>

namespace monkey::net {


/**
 * TCP/IPv4 Socket.
 *
 * Set `ip` and `port` accrodingly, then call `connect` to `start` open connection(s). 
 */
struct Socket4 : public monkey::net::PromisedSocketIo {

    /**
     * In server mode, this is which ip to listen.
     * In client mode, this is which ip to connect to.
     */
    IP4Addr ip = { 0 };


    /**
     * In server mode, this is our port listens for connections.
     * In client mode, this is server's port.
     */
    adl::uint16_t port = 0;


    /**
     * For client mode.
     *
     * This should do 2 things:
     *   1. Create socket
     *   2. Connect to server
     */
    virtual monkey::Status connect();


    // For server mode.
    virtual monkey::Status start(adl::size_t maxClients = 32);


    /**
     * For server mode.
     * 
     * Socket4 returned is a connection client to the other side. It might be INVALID.
     * So check whether `valid()` before using that connection.
     */
    Socket4 accept(bool ignoreError = false);

};


}  // namespace monkey::net

