/*
 * Protocol V1 Connection
 *
 * Created on 2025.1.21 at Xiangzhou, Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#include <monkey/net/protocol/Protocol1Connection.h>
#include <monkey/crypto/rc4.h>

#define RECV_AND_HANDLE_RESPONSE MONKEY_PROTOCOL_RECV_AND_HANDLE_RESPONSE

namespace monkey::net {

using protocol::Msg;
using protocol::MsgType;
using protocol::Response;


void Protocol1Connection::LastError::set(protocol::Response* response, const char* api) {
    set(response->code, response->msg, response->msgLen, api);
}


void Protocol1Connection::LastError::set(
    adl::uint32_t code, 
    const void* msg, 
    adl::size_t msgLen, 
    const char* api
) {
    this->code = code;
    this->api = api;

    adl::ByteArray b;
    b.append(msg, msgLen);
    this->msg = b.toString();
}


void Protocol1Connection::LastError::clear() {
    code = 0;
    msg.clear();
    api.clear();
}


Status Protocol1Connection::sendResponse(
    const adl::uint32_t code,
    const adl::size_t msgLen,
    const void* msg,
    const adl::size_t paimonLen,
    const void* paimon
) {
    adl::ByteArray data;
    data.resize(8 + msgLen + paimonLen);
    (* (uint32_t*) &data[0]) = adl::htonl(code);
    (* (uint32_t*) &data[4]) = adl::htonl(uint32_t(msgLen));
    adl::memcpy(data.data() + 8, msg, msgLen);
    adl::memcpy(data.data() + 8 + msgLen, paimon, paimonLen);
    return sendMsg(MsgType::Response, data);
}


Status Protocol1Connection::sendResponse(const uint32_t code, const adl::ByteArray& msg) {
    adl::ByteArray data;
    data.resize(8 + msg.size());
    
    (* (uint32_t*) &data[0]) = adl::htonl(code);
    (* (uint32_t*) &data[4]) = adl::htonl(uint32_t(msg.size()));
    adl::memcpy(data.data() + 8, msg.data(), msg.size());

    Status status = sendMsg(MsgType::Response, data);
    return status;
}


Status Protocol1Connection::sendResponse(const adl::uint32_t code, const adl::TString& msg) {
    return sendResponse(code, adl::ByteArray {msg.c_str()});
}


Status Protocol1Connection::sendResponse(const adl::uint32_t code, const char* msg) {
    return sendResponse(code, adl::ByteArray {msg});
}



Status Protocol1Connection::recvResponse(Response** response) {
    Msg* rawMsg = nullptr;
    Status libResult = recvMsg(&rawMsg, MsgType::Response);

    if (libResult != Status::SUCCESS) {
        return libResult;
    }


    *response = (Response*) rawMsg;


    (*response)->code = adl::ntohl((*response)->code);
    (*response)->msgLen = adl::ntohl((*response)->msgLen);

    return Status::SUCCESS;
}


// ------ 0x1001 : Auth ------

Status Protocol1Connection::auth(const adl::ByteArray& key) {
    adl::ByteArray challenge;
    Status status = recvAuth(challenge);
    if (status != Status::SUCCESS)
        return status;

    crypto::rc4Inplace(challenge, key);
    if ((status = sendResponse(0, challenge)) != Status::SUCCESS) {
        return status;
    }

    protocol::Response* response = nullptr;
    status = recvResponse(&response);
    if (status != Status::SUCCESS)
        return status;

    if (response->code != 0)
        status = Status::PROTOCOL_ERROR;

    this->freeMsg(response);
    return status;
}


Status Protocol1Connection::auth(
    const adl::HashMap<adl::int64_t, adl::ByteArray>& appsKeyring, 
    const adl::ArrayList<adl::ByteArray>& memoryNodesKeyring
) {
    return auth(&appsKeyring, &memoryNodesKeyring);
}


Status Protocol1Connection::auth(
    const adl::HashMap<adl::int64_t, adl::ByteArray>* appsKeyring, 
    const adl::ArrayList<adl::ByteArray>* memoryNodesKeyring
) {
    adl::ByteArray challenge { "fyt's score is A+" };  // TODO

    Status status;
    if ( (status = sendAuth(challenge)) != Status::SUCCESS )
        return status;

    Response* response = nullptr;
    if ((status = recvResponse(&response)) != Status::SUCCESS)
        return status;

    adl::ByteArray cipher { response->msg, response->msgLen };

    bool verified = false;

    // check if app
    
    if (appsKeyring) {
        for (const auto& it : *appsKeyring) {
            verified = crypto::rc4Verify(it.second, challenge, cipher);
            if (verified) {
                appId = it.first;
                nodeType = NodeType::App;
                break;
            }
        }
    }

    // check if memory node
    
    if (!verified && memoryNodesKeyring) {
        if (crypto::rc4Verify(*memoryNodesKeyring, cipher, challenge) != -1) {
            nodeType = NodeType::MemoryNode;
            verified = true;
        }
        else {
            nodeType = NodeType::Unknown;
        }
    }

    sendResponse(!!(nodeType == NodeType::Unknown), nullptr);

    this->freeMsg(response);
    response = nullptr;
    return Status::SUCCESS;
}



Status Protocol1Connection::sendAuth(const adl::ByteArray& challenge) {
    return sendMsg(MsgType::Auth, challenge);
}


Status Protocol1Connection::recvAuth(adl::ByteArray& challenge) {
    protocol::Msg* msg = nullptr;
    Status status = recvMsg(&msg, MsgType::Auth);
    if (status != Status::SUCCESS)
        return status;

    if (!challenge.resize(msg->header.length)) {
        adl::defaultAllocator.free(msg);
        return Status::OUT_OF_RESOURCE;
    }

    adl::memcpy(challenge.data(), msg->data, challenge.size());
    this->freeMsg(msg);
    return Status::SUCCESS;
}

// ------ 0x1100 : Get Identity Keys ------

struct GetIdentityKeysKeyHeaderPacked {
    Protocol1Connection::ReplyGetIdentityKeysParams::NodeType nodeType;
    Protocol1Connection::ReplyGetIdentityKeysParams::KeyType keyType;
    adl::int8_t reserved0[2] = {0};

    adl::int64_t offset; 
    adl::int32_t len; 

    adl::int64_t id;
    adl::int8_t reserved1[16] = {0}; 
} __packed;


Status Protocol1Connection::ReplyGetIdentityKeysParams::addKey(
    NodeType nodeType,
    KeyType keyType,
    const adl::ByteArray& key,
    adl::int64_t id
) {

    // check params

    if (nodeType != NodeType::App && nodeType != NodeType::MemoryNode)
        return Status::INVALID_PARAMETERS;
    
    if (keyType != KeyType::RC4)  // only RC4 allowed.
        return Status::INVALID_PARAMETERS;
    
    if (key.size() > INT32_MAX)
        return Status::INVALID_PARAMETERS;
    

    // allocate header

    const adl::size_t headerSize = sizeof(GetIdentityKeysKeyHeaderPacked);

    if ( ! keyHeaders.resize(keyHeaders.size() + headerSize) ) {
        Genode::error("Vesper Protocol ReplyGetIdentityKeys: Failed to add key. Out of resource.");
        Genode::error("> Resize failed. Target size: ", keyHeaders.size() + headerSize);
        return Status::OUT_OF_RESOURCE;
    }

    auto head = (GetIdentityKeysKeyHeaderPacked*) (keyHeaders.data() + keyHeaders.size() - headerSize);

    adl::memset(head, 0, headerSize);

    head->nodeType = nodeType;
    head->keyType = keyType;
    head->offset = (adl::int64_t) keys.size();
    head->len = (adl::int32_t) key.size();
    head->id = id;

    if (keys.append(key.data(), key.size()) != 0 /* Indicates error. */) {
        keyHeaders.resize(keyHeaders.size() - headerSize);
        Genode::error("Vesper Protocol ReplyGetIdentityKeys: Failed to add key. Out of resource.");
        return Status::OUT_OF_RESOURCE;
    }

#if VESPER_PROTOCOL_DEBUG
    Genode::log("Key added to reply get identity keys params. ", key.toString().c_str());
    Genode::log("> keyHeaders.size() = ", keyHeaders.size());
#endif
    return Status::SUCCESS;
}


Status Protocol1Connection::sendGetIdentityKeys() {
    return sendMsg(MsgType::GetIdentityKeys);
}


Status Protocol1Connection::replyGetIdentityKeys(const ReplyGetIdentityKeysParams& params) {

    adl::ByteArray msg;
    
    if (!msg.resize(8 + params.keyHeaders.size() + params.keys.size()))
        return Status::OUT_OF_RESOURCE;

#if VESPER_PROTOCOL_DEBUG
    Genode::log("replyGetIdentityKeys");
    Genode::log("> params.keyHeaders.size() = ", params.keyHeaders.size());
    Genode::log("> params.keys.size()       = ", params.keys.size());
#endif

    adl::uint64_t nkeys = params.keyHeaders.size() / sizeof(GetIdentityKeysKeyHeaderPacked);
    * (adl::uint64_t *) msg.data() = adl::htonq(nkeys);

    adl::memcpy(msg.data() + 8, params.keyHeaders.data(), params.keyHeaders.size());
    adl::memcpy(msg.data() + 8 + params.keyHeaders.size(), params.keys.data(), params.keys.size());

    auto pHeader = (GetIdentityKeysKeyHeaderPacked*) (msg.data() + 8);

    while ((void*) pHeader < msg.data() + 8 + params.keyHeaders.size()) {
        pHeader->len = adl::htonl(pHeader->len);

        pHeader->offset = adl::htonq(
            pHeader->offset + adl::int64_t(sizeof(nkeys)) + adl::int64_t(params.keyHeaders.size())
        );

        pHeader->id = adl::htonq(pHeader->id);

        pHeader++;
    }

    return sendResponse(0, msg);
}


Status Protocol1Connection::appreciateGetIdentityKeys(
    protocol::Response& response,
    void* data,
    void (*record) (
        ReplyGetIdentityKeysParams::NodeType nodeType, 
        ReplyGetIdentityKeysParams::KeyType keyType, 
        const adl::ByteArray& key, 
        adl::int64_t id, 
        void* data
    )
) {
    if (response.code != 0) {
        Genode::error("Vesper Protocol [appreciate GetIdentityKeys]: response.code is not 0.");
        return Status::PROTOCOL_ERROR;
    }
    
    auto pMsg = (const char*) response.msg;
    auto nkeys = adl::ntohq (* (adl::uint64_t *) pMsg);
    
    adl::size_t msgLen = response.header.length - 8;  // Response body minus error-code and msg-len.
    if (msgLen < 8 /* nkeys tooks 8 bytes */ + nkeys * sizeof(GetIdentityKeysKeyHeaderPacked)) {
        Genode::error("Vesper Protocol [appreciate GetIdentityKeys]: msg too short.");
        return Status::PROTOCOL_ERROR;
    }

    
    auto pHeader = (GetIdentityKeysKeyHeaderPacked*) (pMsg + 8);

    for (adl::size_t i = 0; i < nkeys; i++) {
        auto& header = pHeader[i];

        const auto RC4Type = ReplyGetIdentityKeysParams::KeyType::RC4;
        const auto AppType = ReplyGetIdentityKeysParams::NodeType::App;
        const auto MemoryNodeType = ReplyGetIdentityKeysParams::NodeType::MemoryNode;

        if (header.keyType != RC4Type || (header.nodeType != MemoryNodeType && header.nodeType != AppType)) {
            Genode::error("Vesper Protocol [appreciate GetIdentityKeys]: Bad Type.");
            Genode::error(
                "> key type: ", 
                adl::uint32_t(header.keyType), 
                ", node type: ", 
                adl::uint32_t(header.nodeType)
            );
            return Status::PROTOCOL_ERROR;
        }

        auto offset = adl::ntohq(header.offset);
        auto len = adl::ntohl(header.len);
        auto id = adl::ntohq(header.id);
        if (offset + len > adl::int64_t(msgLen)) {
            Genode::error("Vesper Protocol [appreciate GetIdentityKeys]: Out of bound.");
            Genode::error("> ", offset, " + ", len, " IS NOT LARGER THAN ", msgLen);
            return Status::PROTOCOL_ERROR;
        }
        
        adl::ByteArray key;
        if (key.append(pMsg + offset, (adl::size_t) len) != 0) {
            Genode::error("Vesper Protocol [appreciate GetIdentityKeys]: Out of resource.");
            return Status::OUT_OF_RESOURCE;
        }
        
        record(header.nodeType, header.keyType, key, id, data);
    }

    return Status::SUCCESS;
}


// ------ 0x2000 : Memory Node Show ID ------


Status Protocol1Connection::sendMemoryNodeShowId(adl::int64_t id) {
    adl::int64_t netOrderId = adl::htonq(id);
    return sendMsg(
        protocol::MsgType::MemoryNodeShowId,
        &netOrderId,
        8
    );
}


Status Protocol1Connection::decodeMemoryNodeShowId(protocol::Msg* msg, adl::int64_t* id) {
    
    if (msg->header.length < sizeof(adl::int64_t))
        return Status::PROTOCOL_ERROR;

    *id = adl::ntohq( *(adl::int64_t*) msg->data);
    return Status::SUCCESS;
}


// ------ 0x2001 : Memory Node Clock In ------


Status Protocol1Connection::memoryNodeClockIn(adl::int64_t* id, IP4Addr ip, adl::uint16_t port) {
    Status status = sendMemoryNodeClockIn(ip.ui32, port);
    if (status != Status::SUCCESS)
        return Status::PROTOCOL_ERROR;

    RECV_AND_HANDLE_RESPONSE(
        auto& res = response;
        if (res->header.length < 16) {
            Genode::error("Bad response. Bad length: ", adl::uint64_t(res->header.length));
            status = Status::PROTOCOL_ERROR;
        }
        else {
            *id = adl::ntohq(* (adl::int64_t *) res->msg);
        }
    );

    return status;
}


Status Protocol1Connection::sendMemoryNodeClockIn(adl::uint32_t tcp4Ip, adl::uint16_t port) {
    adl::uint8_t data[12];
    *(adl::int32_t*) data = adl::htonl(4);  // TCP Protocol Version 4.
    *(adl::uint32_t*) (data + 4) = adl::htonl((adl::uint32_t) port);
    *(adl::uint32_t*) (data + 8) = tcp4Ip;
    return sendMsg(MsgType::MemoryNodeClockIn, data, sizeof(data));
}


Status Protocol1Connection::decodeMemoryNodeClockIn(
    protocol::Msg* msg, 
    adl::int32_t* tcpVer, 
    adl::uint16_t* port, 
    adl::uint8_t ip[]
) {
    if (msg->header.length < 12) {
        return Status::PROTOCOL_ERROR;
    }
    
    *tcpVer = adl::ntohl( *(adl::int32_t*) (msg->data + 0) );
    *port = (adl::uint16_t) adl::ntohl( *(adl::uint32_t*) (msg->data + 4) );

    if (*tcpVer != 4)
        return Status::PROTOCOL_ERROR;
    
    adl::memcpy(ip, msg->data + 8, (*tcpVer == 4 ? 4 : 16));
    return Status::SUCCESS;
}



// ------ 0x2004 : Locate Memory Nodes ------

Status Protocol1Connection::sendLocateMemoryNodes() {
    return sendMsg(MsgType::LocateMemoryNodes);
}


Status Protocol1Connection::locateMemoryNodes(adl::ArrayList<MemoryNodeInfo>& out) {
    Status status = sendLocateMemoryNodes();
    if (status != Status::SUCCESS)
        return status;

    RECV_AND_HANDLE_RESPONSE(
        out.clear();
        auto& r = response;

        auto ENTRY_SIZE = sizeof(LocateMemoryNodeNodeInfoEntry);
        for (adl::size_t off = 0; off < r->header.length - 8; off += ENTRY_SIZE) {
            auto& entryPacked = *(LocateMemoryNodeNodeInfoEntry*) (r->msg + off);
            
            auto tcpVer = adl::ntohl(entryPacked.tcpVersion);
            if (tcpVer != 4) {
                Genode::error(
                    "VesperProtocol [locate memory nodes]: Entry on offset ", 
                    off, 
                    " 's tcp version is ", 
                    (tcpVer + 0),  // member of packed struct cannot in Genode::log directly. 
                    ". Ignored."
                );

                continue;
            }
            
            out.append({});
            out.back().id = adl::ntohq(entryPacked.id);
            out.back().port = (adl::uint16_t) adl::ntohl(entryPacked.port);
            out.back().tcpVersion = tcpVer;
            out.back().ip = entryPacked.inet4addr;
        }
    );

    return status;
}


// ------ 0x3001 : Try Alloc ------

Status Protocol1Connection::sendTryAlloc(adl::size_t blockSize, adl::size_t nBlocks) {
    adl::ByteArray data;
    if (!data.resize(16)) {
        return Status::OUT_OF_RESOURCE;
    }

    auto a = (adl::uint64_t*) data.data();
    a[0] = adl::htonq(adl::uint64_t(blockSize));
    a[1] = adl::htonq(adl::uint64_t(nBlocks));
    return sendMsg(MsgType::TryAlloc, data);
}


Status Protocol1Connection::decodeTryAlloc(Msg* msg, adl::size_t* blockSize, adl::size_t* nBlocks) {
    // Generated by Google Gemini 2.0 Flash. Checked by GTY.
    
    if (msg->header.length < 2 * sizeof(adl::uint64_t)) {
        return Status::PROTOCOL_ERROR;
    }

    *blockSize = adl::ntohq(*reinterpret_cast<adl::uint64_t*>(msg->data));
    *nBlocks = adl::ntohq(*reinterpret_cast<adl::uint64_t*>(msg->data + 8));

    return Status::SUCCESS;
}


Status Protocol1Connection::replyTryAlloc(adl::ArrayList<adl::int64_t> blockIds) {
    adl::ArrayList<adl::int64_t> data = blockIds;
    
    for (auto& it : data) {
        it = adl::htonq(it);
    }

    return sendResponse(0, data.size() * sizeof(data[0]), data.data());
}


Status Protocol1Connection::tryAlloc(
    adl::size_t blockSize, 
    adl::size_t nBlocks, 
    adl::ArrayList<adl::int64_t>& idOut
) {
    Status status = sendTryAlloc(blockSize, nBlocks);
    if (status != Status::SUCCESS)
        return status;


    RECV_AND_HANDLE_RESPONSE(
        auto& r = response;
        idOut.clear();
        for (auto p = (adl::int64_t*) r->msg; (const char*) p < r->msg + r->header.length - 8; p++) {
            idOut.append(adl::ntohq(*p));
        }
    );

    return status;
}



// ------ 0x3002 : Read Block ------

Status Protocol1Connection::sendReadBlock(adl::int64_t blockId) {
    adl::ByteArray data;
    if (!data.resize(sizeof(blockId)))
        return Status::OUT_OF_RESOURCE;

    *(adl::int64_t*) data.data() = adl::htonq(blockId);
    return sendMsg(MsgType::ReadBlock, data);
}


Status Protocol1Connection::decodeReadBlock(protocol::Msg* msg, adl::int64_t* blockId) {
    // Generated by Google Gemini 2.0 Flash. Checked by GTY.
    
    if (msg->header.length < sizeof(adl::int64_t)) {
        return Status::PROTOCOL_ERROR;
    }
    *blockId = adl::ntohq(*reinterpret_cast<adl::int64_t*>(msg->data));
    return Status::SUCCESS;
}


Status Protocol1Connection::replyReadBlock(const void* data, adl::size_t size) {
    return sendResponse(0, size, data);
}



Status Protocol1Connection::readBlock(adl::int64_t blockId, void* buf, adl::size_t bufSize) {
    Status status = sendReadBlock(blockId);

    if (status != Status::SUCCESS)
        return status;

    RECV_AND_HANDLE_RESPONSE(
        auto& r = response;
        auto blockSize = r->header.length - 8;
        if (blockSize != bufSize) {
            Genode::error(
                "Vesper Protocol [read block]: bufSize ", 
                bufSize, 
                " not match block size ", 
                blockSize
            );
            status = Status::PROTOCOL_ERROR;
        }
        else {
            adl::memcpy(buf, r->msg, bufSize);
        }
    );

    return status;
}


// ------ 0x3003 : Write Block ------

Status Protocol1Connection::sendWriteBlock(adl::int64_t blockId, const void* data, adl::size_t size) {
    adl::ByteArray msg;
    
    if (!msg.resize(size + 8)) {
        return Status::OUT_OF_RESOURCE;
    }

    *((adl::int64_t*) msg.data()) = adl::htonq(blockId);
    adl::memcpy(msg.data() + 8, data, size);

    return sendMsg(MsgType::WriteBlock, msg);
}


Status Protocol1Connection::decodeWriteBlock(protocol::Msg* msg, adl::int64_t* id, adl::ByteArray& data) {
    auto len = msg->header.length;
    if (len < 8) {  // Block ID
        return Status::PROTOCOL_ERROR;
    }

    *id = *(adl::int64_t*) msg->data;
    *id = adl::ntohq(*id);

    auto blockSize = len - 8;
    if (!data.resize(blockSize)) {
        return Status::OUT_OF_RESOURCE;
    }

    adl::memcpy(data.data(), msg->data + 8, blockSize);

    return Status::SUCCESS;
}



Status Protocol1Connection::writeBlock(adl::int64_t blockId, const void* data, adl::size_t size) {
    Status status = Status::SUCCESS;

    if ((status = sendWriteBlock(blockId, data, size)) != Status::SUCCESS) {
        return status;
    }

    RECV_AND_HANDLE_RESPONSE({});

    return status;
}




// ------ 0x3004 : Check Avail Mem ------

Status Protocol1Connection::sendCheckAvailMem() {
    return sendMsg(MsgType::CheckAvailMem);
}


Status Protocol1Connection::replyCheckAvailMem(adl::size_t availMem) {
    adl::uint64_t availMemNetOrder = adl::htonq((adl::uint64_t) availMem);
    return sendResponse(0, sizeof(availMem), &availMemNetOrder);
}


Status Protocol1Connection::checkAvailMem(adl::size_t* availMem) {
    Status status = sendCheckAvailMem();
    if (status != Status::SUCCESS) {
        return status;
    }

    RECV_AND_HANDLE_RESPONSE(
        *availMem = (adl::size_t) adl::htonq(*(adl::uint64_t*) response->msg);
    );

    return status;
}




// ------ 0x3005 : Free Block ------

Status Protocol1Connection::sendFreeBlock(adl::int64_t blockId) {
    auto bId_netOrder = adl::htonq(blockId);
    return sendMsg(MsgType::FreeBlock, &bId_netOrder, sizeof(bId_netOrder));
}


Status Protocol1Connection::decodeFreeBlock(Msg* msg, adl::int64_t* blockId) {
    // Generated by Google Gemini 2.0 Flash. Checked by GTY.

    if (msg->header.length < sizeof(adl::int64_t)) {
        return Status::PROTOCOL_ERROR;
    }

    *blockId = adl::ntohq(*reinterpret_cast<adl::int64_t*>(msg->data));

    return Status::SUCCESS;
}


Status Protocol1Connection::freeBlock(adl::int64_t blockId) {
    // Generated by Google Gemini 2.0 Flash. Checked by GTY.
    
    Status status = sendFreeBlock(blockId);
    if (status != Status::SUCCESS) {
        return status;
    }

    RECV_AND_HANDLE_RESPONSE({});

    return status;
}


}
