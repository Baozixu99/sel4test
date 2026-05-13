/*
 * Monkey Net :: TcpIo
 *
 * Created on 2024.12.27 at Minhang, Shanghai
 * 
 * 
 * feng.yt [at]  sjtu  [dot] edu [dot] cn
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#pragma once

#include <adl/sys/types.h>
#include <monkey/Status.h>
#include <unistd.h>

namespace monkey::net {


class TcpIo {

public:
    virtual ~TcpIo() {}

    /**
     * 
     * @param len Buffer size.
     */
    virtual adl::int64_t recv(void* buf, adl::size_t len) = 0;


    /**
     * 
     * @param len Size of data.
     */
    virtual adl::int64_t send(const void* buf, adl::size_t len) = 0;

};


/**
 * Promised Socket IO is NOT closed automatically.
 * You should call `close` as soon as you are finished with it.
 */
class PromisedSocketIo : public TcpIo {
public:
    int socketFd = -1;


    /**
     * This method is reentrant.
     *  
     * This method is a no-op if the socket has already been closed.
     *
     * You should call this as soon as you doesn't use this socket.
     */
    virtual void close();

    virtual ~PromisedSocketIo() override { 
    
    }

    virtual bool valid() { return socketFd > 1; }

    virtual adl::int64_t recv(void* buf, adl::size_t len) override;

    virtual adl::int64_t send(const void* buf, adl::size_t len) override;

};



}  // namespace monkey::net
