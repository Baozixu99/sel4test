/*
 * Protocol Connection Base
 *
 * Created on 2025.1.21 at Xiangzhou, Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */

#pragma once

#include <monkey/log.h>

#include <monkey/net/config.h>

#include <monkey/net/TcpIo.h>
#include <monkey/net/protocol/defines.h>
#include <monkey/net/Socket4.h>

#include <netinet/in.h>

#include <monkey/net/protocol/defines.h>



/**
 * This macro is designed for simple "ping-pong" like requests.
 *
 * This macro should be used after sending request to server.
 *
 * The macro do follows:
 * 1. recv response (return if error).
 * 2. if response code is not 0, record and set `status` to error type.
 * 3. if response code is 0, run code in `onSuccess`. `response` is used for raw msg.
 * 4. free up response message.
 */
#define MONKEY_PROTOCOL_RECV_AND_HANDLE_RESPONSE(onSuccess) \
    do { \
        Response* response = nullptr; \
        if ((status = recvResponse(&response)) != Status::SUCCESS) { \
            return status; \
        } \
        if (response->code != 0) { \
            MONKEY_LOG_ERROR( \
                "VesperProtocol: Response code is ", \
                (response->code + 0), \
                " in api ", \
                __FUNCTION__ \
            ); \
            lastError.set(response, __FUNCTION__); \
            MONKEY_LOG_ERROR( \
                "> Error message is: ", \
                (lastError.msg.c_str() ? lastError.msg.c_str() : "<EMPTY>") \
            ); \
            status = Status::PROTOCOL_ERROR; \
        } \
        else { \
            onSuccess \
        } \
        if (response) { \
            this->freeMsg(response); \
            response = nullptr; \
        } \
    } while (0)


namespace monkey::net {

/**
 * Agent for a Vesper Protocol based socket connection.
 *
 * Method types:
 * - send       [TYPE] : Send a [TYPE] msg to other.
 * - decode     [TYPE] : Decode a [TYPE] msg received. [TYPE] should be checked.
 * - reply      [TYPE] : Send a response msg to reply a received [TYPE] msg.
 * - appreciate [TYPE] : Understand the reply of [TYPE] request. `appreciate` often appreciates `response`.
 *                       You should ensure msg passed to method is really the response of [TYPE] request.
 * - (do)       [TYPE] : Do the entire [TYPE] procedure as client (or server if specific method called).
 * 
 *
 * 
 *    Client                  Server
 *    ==============================
 *
 *         (send)                        \
 *            ---------->       .        |
 *                              .        |
 *                           (decode)    |
 *                              .        \
 *                              .         >  "one click"
 *                   (reply)             /
 *      .   <-----------                 |
 *      .                                |
 *      .                                |
 *  (appreciate)                         /
 *
 *
 * This class only maintains basic info for connection, so it can be copied freely.
 * Do not store data like `request counter` in it.
 */
class ProtocolConnection : public Socket4 {
public:

    // Static methods

    inline static const char* magic() { return protocol::MAGIC; }
    inline static adl::size_t magicLen() { return protocol::MAGIC_LEN; }

    inline static bool magicMatch(const adl::uint8_t magic[]) {
        return * (const adl::uint32_t*) ProtocolConnection::magic() == * (const adl::uint32_t*) magic;
    }


    // Basic info methods

    /**
     * This marks current protocol version supported by this object, 
     * not the one protocol is using.
     * 
     * Each connection should starts with a version 0 connection, then upgrade to 
     * certain version after "Hello".
     *
     * See `Protocol.md` for details.
     */
    virtual adl::int64_t version() { 
        return 0;  // Override this for each version. 
    }


    // Connection operations


    inline virtual void makeHeader(
        protocol::Header* header, 
        adl::uint32_t type, 
        adl::uint64_t length, 
        bool netOrder = true,
        bool copyMagic = true
    ) {
        if (copyMagic) {
            adl::memcpy(header->magic, magic(), magicLen());
        }

        header->type = netOrder ? adl::htonl(type) : type;
        header->length = netOrder ? adl::htonq(length) : length;
    }


    inline virtual protocol::Header makeHeader(adl::uint32_t type, adl::uint64_t length, bool netOrder = true) {
        protocol::Header header;
        makeHeader(&header, type, length, netOrder, false);
        return header;  // We believe NRV would optimize this.
    }


    virtual Status sendMsg(
        protocol::MsgType type, 
        const void* data = nullptr,
        adl::uint64_t dataLen = 0
    );


    virtual Status sendMsg(
        protocol::MsgType type, 
        const adl::ByteArray& data
    );



    virtual Status recvHeader(protocol::Header*);


    /**
    * The caller is responsible for freeing `msg` by calling freeMsg.
    * when monkey::Status is SUCCESS.
    *
    * @param type Ensure type. Set to MsgType::None to disable this check.
    */
    virtual Status recvMsg(protocol::Msg** msg, protocol::MsgType type = protocol::MsgType::None);
    virtual inline void freeMsg(protocol::Msg* msg) {
        if (msg) {
            adl::defaultAllocator.free(msg);
        }
    }

    virtual inline void freeMsg(protocol::Response* msg) {
        this->freeMsg((protocol::Msg*) msg);
    }



    // ------ 0x1000 : Hello ------

    enum class HelloMode : bool {
        SERVER, CLIENT
    };

    virtual Status hello(
        const adl::ArrayList<adl::int64_t>& protocolVersions,
        HelloMode,
        adl::int64_t* finalVersion
    ) final;

    /**
     * Use a specified version of protocol. If failed, no more negotiation needed.
     *
     * @param version The version you want to use.
     * @param serverMode Whether works in server mode.
     *
     * @return Status If SUCCESS, negotiation success.
     */
    virtual Status hello(adl::int64_t version, HelloMode) final;

    virtual Status sendHello(const adl::ArrayList<adl::int64_t>& protocolVersions) final;
    virtual Status recvHello(adl::ArrayList<adl::int64_t>& out) final;


};


}