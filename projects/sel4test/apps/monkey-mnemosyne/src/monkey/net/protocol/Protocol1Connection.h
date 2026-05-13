/*
 * Protocol V1 Connection
 *
 * Created on 2025.1.21 at Xiangzhou, Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */

#pragma once

#include <monkey/net/protocol/ProtocolConnection.h>
#include <monkey/net/protocol/ProtocolConnectionDock.h>
#include <adl/collections/HashMap.hpp>

namespace monkey::net {


/**
 * @deprecated Use latest protocol version is suggested.
 */
class Protocol1Connection : public ProtocolConnection {
public:
    struct MemoryNodeInfo {
        adl::int64_t id;
        adl::int32_t tcpVersion;
        adl::uint16_t port;

        IP4Addr ip;
    };

    enum class NodeType {
        MemoryNode,
        App,
        Unknown,
        Other
    } nodeType = NodeType::Other;


    /**
     * Other side's id.
     */
    union {
        adl::int64_t memoryNodeId = -1;
        adl::int64_t appId;
        adl::int64_t nodeId;
    };


    static const adl::int64_t VERSION = 1;
    virtual adl::int64_t version() override { return VERSION; }


    /** 
     * errno: only remembers last failed response received in one-click methods.
     */
    struct LastError {
        adl::uint32_t code = 0;
        adl::TString msg;
        adl::TString api;

        void set(protocol::Response* response, const char* api = nullptr);
        void set(adl::uint32_t code, const void* msg, adl::size_t msgLen, const char* api = nullptr);
        void clear();
    } lastError;


    // Connection operations


    Status sendResponse(
        const adl::uint32_t code,
        const adl::size_t msgLen,
        const void* msg,
        const adl::size_t paimonLen = 0,
        const void* paimon = nullptr
    );

    Status sendResponse(const adl::uint32_t code, const adl::ByteArray& msg);
    Status sendResponse(const adl::uint32_t code, const adl::TString& msg);
    Status sendResponse(const adl::uint32_t code, const char* msg = nullptr);



    /**
     * The caller is responsible for freeing `response`  
     * when monkey::Status is SUCCESS.
     */
    Status recvResponse(protocol::Response** response);



    // ------ 0x1001 : Auth ------

    Status sendAuth(const adl::ByteArray& challenge);
    Status recvAuth(adl::ByteArray& challenge);

    /**
     * Client-mode auth: Show identity to server. 
     */
    Status auth(const adl::ByteArray& key);

    /**
     * Server-mode auth: Check client's identity.
     *
     * @return monkey::Status::SUCCESS Only means no network error occurred.
     *                                 You should also check whether `this->nodeType` is correct.
     */
    Status auth(
        // app id -> app key
        const adl::HashMap<adl::int64_t, adl::ByteArray>& appsKeyring, 
        const adl::ArrayList<adl::ByteArray>& memoryNodesKeyring
    );


    /**
     * Server-mode auth: Check client's identity. 
     * This method works just like the one above except: You can set keyring to nullptr
     * to ignore some type(s).
     *
     * @return monkey::Status::SUCCESS Only means no network error occurred.
     *                                 You should also check whether `this->nodeType` is correct.
     */
    Status auth(
        // app id -> app key
        const adl::HashMap<adl::int64_t, adl::ByteArray>* appsKeyring,
        const adl::ArrayList<adl::ByteArray>* memoryNodesKeyring
    );


    // ------ 0x1100 : Get Identity Keys ------

    struct ReplyGetIdentityKeysParams {
        adl::ByteArray keyHeaders;
        adl::ByteArray keys;

        enum class NodeType : adl::int8_t {
            App = 0,
            MemoryNode = 1
        };

        enum class KeyType : adl::int8_t {
            RC4 = 0
        };

        monkey::Status addKey(
            NodeType nodeType, 
            KeyType keyType, 
            const adl::ByteArray& key,
            adl::int64_t id = 0  // ignored for Memory Node
        ); 
    };


    Status sendGetIdentityKeys();
    Status replyGetIdentityKeys(const ReplyGetIdentityKeysParams&);
    Status appreciateGetIdentityKeys(
        protocol::Response& response,
        void* data,

        /**
         * Called for each key.
         *
         * @param data Same as `data` passed to `appreciate`.
         */
        void (*record) (
            ReplyGetIdentityKeysParams::NodeType nodeType, 
            ReplyGetIdentityKeysParams::KeyType keyType, 
            const adl::ByteArray& key, 
            adl::int64_t id, 
            void* data
        )
    );


    // ------ 0x2000 : Memory Node Show ID ------

    Status sendMemoryNodeShowId(adl::int64_t id);
    Status decodeMemoryNodeShowId(protocol::Msg* msg, adl::int64_t* id);


    // ------ 0x2001 : Memory Node Clock In ------


    Status memoryNodeClockIn(adl::int64_t* id, IP4Addr ip, adl::uint16_t port);


    /**
     * For TCP4. 
     */
    Status sendMemoryNodeClockIn(adl::uint32_t tcp4Ip, adl::uint16_t port);

    /**
     * 
     * 
     * @param ip Should be at least 16 bytes (array length at least 16).
     *           For TCP4, ip[0] to ip[3] is filled.
     *           For TCP6, ip[0] to ip[15] is filled.
     * 
     */
    Status decodeMemoryNodeClockIn(
        protocol::Msg* msg, 
        adl::int32_t* tcpVer, 
        adl::uint16_t* port, 
        adl::uint8_t ip[]
    );


    // ------ 0x2004 : Locate Memory Nodes ------

    struct LocateMemoryNodeNodeInfoEntry {
        adl::int64_t id;
        adl::int32_t tcpVersion;  // must be 4 (in net order)
        adl::uint32_t port;

        adl::uint32_t inet4addr;
        adl::int8_t padding[12];
    } __packed;

    Status sendLocateMemoryNodes();
    Status locateMemoryNodes(adl::ArrayList<MemoryNodeInfo>&);


    // ------ 0x3001 : Try Alloc ------

    Status sendTryAlloc(adl::size_t blockSize, adl::size_t nBlocks);
    Status decodeTryAlloc(protocol::Msg* msg, adl::size_t* blockSize, adl::size_t* nBlocks);
    Status replyTryAlloc(adl::ArrayList<adl::int64_t> blockIds);
    Status tryAlloc(adl::size_t blockSize, adl::size_t nBlocks, adl::ArrayList<adl::int64_t>& idOut);

    // ------ 0x3002 : Read Block ------

    Status sendReadBlock(adl::int64_t blockId);
    Status decodeReadBlock(protocol::Msg* msg, adl::int64_t* blockId);
    Status replyReadBlock(const void* data, adl::size_t size);

    /**
     * Read data from remote, and fill it into `bufSize`. 
     *
     * @param bufSize If 0, api would not check buf's boundary. 
     *                If not 0 and response's size not match, 
     *                error reported with no change to buf.
     */
    Status readBlock(adl::int64_t blockId, void* buf, adl::size_t bufSize = 0);


    // ------ 0x3003 : Write Block ------

    Status sendWriteBlock(adl::int64_t blockId, const void* data, adl::size_t size);
    Status decodeWriteBlock(protocol::Msg* msg, adl::int64_t* id, adl::ByteArray& data);

    Status writeBlock(adl::int64_t blockId, const void* data, adl::size_t size);

    // ------ 0x3004 : Check Avail Mem ------
    
    Status sendCheckAvailMem();
    Status replyCheckAvailMem(adl::size_t availMem);
    Status checkAvailMem(adl::size_t* availMem);


    // ------ 0x3005 : Free Block ------

    Status sendFreeBlock(adl::int64_t blockId);
    Status decodeFreeBlock(protocol::Msg*, adl::int64_t* blockId);
    Status freeBlock(adl::int64_t blockId);

};



MONKEY_NET_IMPL_DOCK_PROTOCOL(Protocol1Connection);


}
