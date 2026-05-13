/*
    Monkey Protocol Connection Version 2


    Created on 2025.6.7 at Wujing, Minhang 
    gongty [at] alumni [dot] tongji [dot] edu [dot] cn
*/

#pragma once

#include <monkey/net/protocol/ProtocolConnectionDock.h>
#include <monkey/net/protocol/Protocol1Connection.h>


namespace monkey::net {

class Protocol2Connection : public Protocol1Connection {
public:

    static const adl::int64_t VERSION = 2;
    virtual adl::int64_t version() override { return VERSION; }
    
    
    // ------ 0x3001 : Try Alloc ------

    Status sendTryAlloc();
    Status decodeTryAlloc(protocol::Msg* msg, adl::size_t* blockSize, adl::size_t* nBlocks) = delete;
    Status replyTryAlloc(adl::int64_t blockId, adl::int64_t dataVer, adl::int64_t readKey, adl::int64_t writeKey);
    Status tryAlloc(
        adl::int64_t* blockId, 
        adl::int64_t* dataVer = nullptr, 
        adl::int64_t* readKey = nullptr, 
        adl::int64_t* writeKey = nullptr);

    
    // ------ 0x3002 : Read Block ------

    inline Status sendReadBlock(adl::int64_t blockId) { 
        return Protocol1Connection::sendReadBlock(blockId);
    }
    inline Status decodeReadBlock(protocol::Msg* msg, adl::int64_t* blockId) {
        return Protocol1Connection::decodeReadBlock(msg, blockId);
    }
    Status replyReadBlock(adl::int64_t dataVer, const void* data);

    /**
     * Read data from remote, and fill it into `bufSize`. 
     *
     * You should ensure `buf` size is enough (not less than 4KB).
     */
    Status readBlock(adl::int64_t blockId, void* buf, adl::int64_t* dataVer = nullptr);


    // ------ 0x3003 : Write Block ------

    inline Status sendWriteBlock(adl::int64_t blockId, const void* data) {
        return Protocol1Connection::sendWriteBlock(blockId, data, 4096);
    }
    inline Status decodeWriteBlock(protocol::Msg* msg, adl::int64_t* id, adl::ByteArray& data) {
        return Protocol1Connection::decodeWriteBlock(msg, id, data);
    }

    Status writeBlock(adl::int64_t blockId, const void* data, adl::int64_t* dataVer = nullptr);

    
    // ------ 0x3006 : Ref Block ------

    Status decodeRefBlock(protocol::Msg* msg, adl::int64_t* accessKey);
    Status refBlock(adl::int64_t accessKey, adl::int64_t* blockId);

    // ------ 0x3007 : Unref Block ------

    
    Status decodeUnrefBlock(protocol::Msg* msg, adl::int64_t* blockId);
    Status unrefBlock(adl::int64_t blockId);

    // ------ 0x3008 : Get Block Data Version ------

    Status decodeGetBlockDataVersion(protocol::Msg* msg, adl::int64_t* id);
    Status getBlockDataVersion(adl::int64_t blockId, adl::int64_t* dataVer);

};



MONKEY_NET_IMPL_DOCK_PROTOCOL(Protocol2Connection);

}  // namespace monkey::net

