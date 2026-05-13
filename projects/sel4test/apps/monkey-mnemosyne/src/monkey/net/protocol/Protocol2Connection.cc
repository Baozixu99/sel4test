/*
    Monkey Protocol Connection Version 2


    Created on 2025.6.7 at Wujing, Minhang 
    gongty [at] alumni [dot] tongji [dot] edu [dot] cn
*/


#include <monkey/net/protocol/Protocol2Connection.h>

using namespace monkey;
using namespace monkey::net;
using monkey::net::protocol::Response;


#define RECV_AND_HANDLE_RESPONSE MONKEY_PROTOCOL_RECV_AND_HANDLE_RESPONSE

// ------ 0x3001 : Try Alloc ------

Status Protocol2Connection::sendTryAlloc() {
    return sendMsg(protocol::MsgType::TryAlloc);
}


Status Protocol2Connection::replyTryAlloc(adl::int64_t blockId, adl::int64_t dataVer, adl::int64_t readKey, adl::int64_t writeKey) {

    struct {
        adl::int64_t blockId;
        adl::int64_t dataVer;
        adl::int64_t rdkey;
        adl::int64_t wrkey;
    } __packed payload;

    payload.blockId = adl::htonq(blockId);
    payload.dataVer = adl::htonq(dataVer);
    payload.rdkey = readKey;
    payload.wrkey = writeKey;

    return sendResponse(0, sizeof(payload), &payload);
}


Status Protocol2Connection::tryAlloc(adl::int64_t* blockId, adl::int64_t* dataVer, adl::int64_t* readKey, adl::int64_t* writeKey) {
    
    Status status = sendTryAlloc();
    if (status != Status::SUCCESS)
        return status;

    
    RECV_AND_HANDLE_RESPONSE(
        auto& r = response;
        
        struct Payload {
            adl::int64_t blockId;
            adl::int64_t dataVer;
            adl::int64_t rdkey;
            adl::int64_t wrkey;
        } __packed;

        if (r->header.length < sizeof(Payload)) {
            Genode::error("TryAlloc: Failed to decode response.");
            status = Status::PROTOCOL_ERROR;
            break;
        }

        Payload* pPayload = (Payload*) r->msg;
        Payload& payload = *pPayload;
        if (blockId)
            *blockId = adl::ntohq(payload.blockId);
        if (dataVer)
            *dataVer = adl::ntohq(payload.dataVer);
        if (readKey)
            *readKey = payload.rdkey;
        if (writeKey)
            *writeKey = payload.wrkey;
    );

    return status;
}



// ------ 0x3002 : Read Block ------




Status Protocol2Connection::replyReadBlock(adl::int64_t dataVer, const void* data) {
    auto header = makeHeader((adl::uint32_t) protocol::MsgType::Response, 4096 + 8 + sizeof(dataVer));

    adl::int64_t dataVerNetOrder = adl::htonq(dataVer);
    auto acc = send(&header, sizeof(net::protocol::Header));

    adl::int32_t zero = 0;

    // code and msgLen all set to 0.
    acc += send(&zero, sizeof(zero));
    acc += send(&zero, sizeof(zero));

    acc += send(&dataVerNetOrder, sizeof(dataVerNetOrder));
    acc += send(data, 4096);

    return (acc == sizeof(header) + 4096 + 8 + sizeof(dataVer)) ? Status::SUCCESS : Status::NETWORK_ERROR;
}


Status Protocol2Connection::readBlock(adl::int64_t blockId, void* buf, adl::int64_t* dataVer) {
    Status status = sendReadBlock(blockId);

    if (status != Status::SUCCESS)
        return status;
    
    RECV_AND_HANDLE_RESPONSE(
        auto& r = response;
        auto blockSize = r->header.length - 8 - sizeof(*dataVer);
        if (blockSize != 4096) {
            Genode::error(
                "Vesper Protocol [read block]: bufSize ", 
                4096, 
                " not match block size ", 
                blockSize
            );
            status = Status::PROTOCOL_ERROR;
        }
        else {
            if (dataVer)
                *dataVer = adl::htonq(* (adl::int64_t*) r->msg);
            adl::memcpy(buf, r->msg + sizeof(dataVer), blockSize /* 4096 */);
        }
    );

    return status;
}



// ------ 0x3003 : Write Block ------



Status Protocol2Connection::writeBlock(adl::int64_t blockId, const void* data, adl::int64_t* dataVer) {
    Status status = sendWriteBlock(blockId, data);

    if (status != Status::SUCCESS) {
        return status;
    }

    RECV_AND_HANDLE_RESPONSE(
        auto& r = response;
        if (r->msgLen != 8) {
            Genode::error("Protocol Error: ", __FUNCTION__);
            status = Status::PROTOCOL_ERROR;
            break;
        }

        if (dataVer) {
            *dataVer = adl::ntohq(* (adl::int64_t*) r->msg);
        }
    );

    return status;
}



// ------ 0x3006 : Ref Block ------

Status Protocol2Connection::decodeRefBlock(protocol::Msg* msg, adl::int64_t* accessKey) {

    if (msg->header.length < sizeof(adl::int64_t)) {
        return Status::PROTOCOL_ERROR;
    }
    
    *accessKey = *(adl::int64_t*) msg->data;
    return Status::SUCCESS;
}


Status Protocol2Connection::refBlock(adl::int64_t accessKey, adl::int64_t* blockId) {
    Status status = sendMsg(protocol::MsgType::RefBlock, &accessKey, sizeof(accessKey));

    
    RECV_AND_HANDLE_RESPONSE(
        auto& r = response;
        // assert response is valid. so skip msglen check.
        
        if (blockId)
            *blockId = adl::ntohq(* (adl::int64_t *) r->msg);
    );

    return status;
}



// ------ 0x3007 : Unref Block ------


Status Protocol2Connection::decodeUnrefBlock(protocol::Msg* msg, adl::int64_t* blockId) {
    if (msg->header.length < sizeof(adl::int64_t)) {
        return Status::PROTOCOL_ERROR;
    }
    
    *blockId = adl::ntohq(*(adl::int64_t*) msg->data);
    return Status::SUCCESS;
}


Status Protocol2Connection::unrefBlock(adl::int64_t blockId) {
    adl::int64_t blockIdNetOrder = adl::htonq(blockId);
    Status status = sendMsg(protocol::MsgType::UnrefBlock, &blockIdNetOrder, sizeof(blockIdNetOrder));
    if (status != Status::SUCCESS) {
        return status;
    }

    RECV_AND_HANDLE_RESPONSE({});

    return status;
}



// ------ 0x3008 : Get Block Data Version ------

Status Protocol2Connection::decodeGetBlockDataVersion(protocol::Msg* msg, adl::int64_t* id) {

    if (msg->header.length < sizeof(adl::int64_t)) {
        return Status::PROTOCOL_ERROR;
    }
    
    *id = adl::ntohq(*reinterpret_cast<adl::int64_t*>(msg->data));
    return Status::SUCCESS;
}


Status Protocol2Connection::getBlockDataVersion(adl::int64_t blockId, adl::int64_t* dataVer) {
    adl::int64_t blockIdNetOrder = adl::htonq(blockId);
    Status status = sendMsg(protocol::MsgType::GetBlockDataVersion, &blockIdNetOrder, sizeof(blockIdNetOrder));
    if (status != Status::SUCCESS) {
        return status;
    }

    RECV_AND_HANDLE_RESPONSE(
        if (dataVer)
            *dataVer = adl::ntohq(*(adl::int64_t*) response->msg);
    );

    return status;
}

