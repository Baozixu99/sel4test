/*
 * Protocol Connection Base
 *
 * Created on 2025.1.21 at Xiangzhou, Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#include <base/log.h>
#include <monkey/net/protocol/ProtocolConnection.h>


#if VESPER_PROTOCOL_DEBUG
#include <monkey/net/hexview.h>
#endif

namespace monkey::net {

using protocol::MsgType;
using protocol::Msg;
using protocol::Header;



Status ProtocolConnection::sendMsg(MsgType type, const void* data, adl::uint64_t dataLen) {
    Header header = makeHeader(adl::uint32_t(type), dataLen);


#if VESPER_PROTOCOL_DEBUG
    {
        adl::TString hex;
        adl::size_t pos = 0;
        debug::hexView(
            1, 
            [&] () {
                if (pos < sizeof(Header)) {
                    return (int) ((char*) &header)[pos++];
                }
                else if (pos - sizeof(Header) < dataLen && /* TODO */ pos - sizeof(Header) < 0x40) {
                    return (int) ((char*) data)[pos++ - sizeof(Header)];
                }
                else {
                    return EOF;
                }
            }, 
            [&] (int ch) {
                hex += char(ch);
            }
        );

        Genode::log("VESPER_PROTOCOL_DEBUG : --- Send Msg -->");
        auto typeMachineOrder = adl::ntohl(header.type);
        Genode::log(
            "> header.type  : ", 
            Genode::Hex(typeMachineOrder),
            " ", protocol::msgTypeToString(MsgType(typeMachineOrder))
        );
        Genode::log("> header.length: ", adl::size_t(adl::ntohq(header.length)));
        Genode::log(hex.c_str());
    }
#endif

    const adl::uint32_t headerSize = sizeof(header);
    adl::uint64_t msgSize = headerSize + dataLen;

    adl::int64_t dataWrote = send(&header, headerSize);
    if (data && dataLen)
        dataWrote += send(data, dataLen);

    // Returns whether all msg sent.
    return dataWrote >= 0 && adl::uint64_t(dataWrote) == msgSize ? Status::SUCCESS : Status::NETWORK_ERROR;
}


Status ProtocolConnection::sendMsg(MsgType type, const adl::ByteArray& data) {
    return sendMsg(type, data.data(), data.size());
}




Status ProtocolConnection::recvHeader(Header* header) {
    adl::uint64_t received = 0;
    while(received < sizeof(Header)){
        adl::int64_t num_bytes = recv( ((char*) header) + received, sizeof(Header) - received);
        if(num_bytes == -1) {
            Genode::error("[Receive_header] Read failed.");
            return Status::NETWORK_ERROR;
        }
        received += uint64_t(num_bytes);
    }

    
    // Check magic.

    if ( !magicMatch(header->magic) ) {
        Genode::error("[Receive Header] Header mismatch!");
        return Status::PROTOCOL_ERROR;
    }


    // Data from network is in network's byte-order. Convert it to native one.

    header->type = adl::ntohl(header->type);
    header->length = adl::ntohq(header->length);

    return Status::SUCCESS;
}


Status ProtocolConnection::recvMsg(Msg** msg, MsgType type) {
    Status libResult;

    Header header;
    if ((libResult = recvHeader(&header)) != Status::SUCCESS) {
        return libResult;
    }

    
    adl::uint64_t msgSize = (adl::uint64_t) header.length + sizeof(Header);
    auto protocolMsg = adl::defaultAllocator.alloc<Msg>(1, true, msgSize);

    if (protocolMsg == nullptr) {
        // out of resource.
        return Status::OUT_OF_RESOURCE;
    }

    adl::memcpy(protocolMsg, &header, sizeof(Header));

    adl::uint64_t received = 0;
    while(received < header.length){
        adl::int64_t num_bytes = recv(((char*) protocolMsg) + sizeof(Header) + received, header.length - received);
        if(num_bytes == -1){ 
            Genode::error("[Recv_msg] Read failed.");
            adl::defaultAllocator.free(protocolMsg);
            return Status::NETWORK_ERROR;
        }
        received += adl::uint64_t(num_bytes);
    }



    if (type != MsgType::None && protocolMsg->header.type != adl::uint32_t(type)) {
        adl::defaultAllocator.free(protocolMsg);
        return Status::PROTOCOL_ERROR;
    }


    *msg = protocolMsg;


#if VESPER_PROTOCOL_DEBUG
    {
        adl::TString hex;
        adl::size_t pos = 0;
        debug::hexView(
            1, 
            [&] () {
                if (pos < sizeof(Header) + header.length && /* TODO */ pos - sizeof(Header) < 0x40) {
                    return (int) ((char*) protocolMsg)[pos++];
                }
                else {
                    return EOF;
                }
            }, 
            [&] (int ch) {
                hex += char(ch);
            }
        );

        Genode::log("VESPER_PROTOCOL_DEBUG : <-- Recv Msg ---");
        Genode::log(
            "> header.type  : ", 
            Genode::Hex(header.type), " ", 
            protocol::msgTypeToString(MsgType(header.type))
        );
        Genode::log("> header.length: ", adl::size_t(header.length));
        Genode::log(hex.c_str());
        Genode::log("Note: Header field is adjusted to host's byte order.");
    }
#endif


    return Status::SUCCESS;
}


// ------ 0x1000 : Hello ------

static Status serverModeHello(
    ProtocolConnection& client,
    const adl::ArrayList<adl::int64_t>& protocolVersions,
    adl::int64_t* finalVersion
) {
    Genode::log("Vesper Protocol [Hello]: Running server-mode Hello.");
    adl::ArrayList<adl::int64_t> clientVers;
    Status status;
    if ((status = client.recvHello(clientVers)) != Status::SUCCESS) {
        Genode::error("Vesper Protocol [Hello]: Failed on phase 1.");
        return status;
    }

    adl::ArrayList<adl::int64_t> versionsSupported;

    for (auto& it : clientVers) {
        if (protocolVersions.contains(it)) {    
            versionsSupported.append(it);
        }
    }

    if (versionsSupported.isEmpty()) {
        Genode::warning("Vesper Protocol [Hello]: Versions supported by client is empty.");
        client.sendHello(adl::ArrayList<adl::int64_t>{});
        return status = Status::PROTOCOL_ERROR;
    }


    if ((status = client.sendHello(versionsSupported)) != Status::SUCCESS) {
        Genode::error("Vesper Protocol [Hello]: Failed on phase 2.");
        return status;
    }
    

    if ((status = client.recvHello(clientVers)) != Status::SUCCESS) {
        Genode::error("Vesper Protocol [Hello]: Failed on phase 3.");
        return status;
    }

    if (clientVers.size() == 1 && versionsSupported.contains(clientVers[0])) {
        *finalVersion = clientVers[0];
        return Status::SUCCESS;
    }
    
    Genode::error("Vesper Protocol [Hello]: No agreement reached with client.");
    return Status::PROTOCOL_ERROR;
}


static Status clientModeHello(
    ProtocolConnection& client,
    const adl::ArrayList<adl::int64_t>& protocolVersions,
    adl::int64_t* finalVersion
) {
    if (protocolVersions.isEmpty()) {
        Genode::error("Vesper Protocol [Hello]: Protocol version list is empty.");
        return Status::INVALID_PARAMETERS;
    }

    Genode::log("Vesper Protocol [Hello]: Running client-mode Hello.");
    
    Status status;
    if ((status = client.sendHello(protocolVersions)) != Status::SUCCESS)
        return status;
    
    adl::ArrayList<adl::int64_t> vers;
    if ((status = client.recvHello(vers)) != Status::SUCCESS) {
        Genode::error("Vesper Protocol [Hello]: Failed on phase 2.");
        return status;
    }

    adl::int64_t maxVer = -1;
    for (auto& it : vers) {
        if (it > maxVer && protocolVersions.contains(it)) {
            maxVer = it;
        }
    }

    if (maxVer == -1)
        return Status::PROTOCOL_ERROR;

    vers.clear();
    vers.append(maxVer);
    if ((status = client.sendHello(vers)) != Status::SUCCESS)
        return status;

    *finalVersion = maxVer;
    return Status::SUCCESS;
}


Status ProtocolConnection::hello(
    const adl::ArrayList<adl::int64_t>& protocolVersions,
    HelloMode mode,
    adl::int64_t* finalVersion
) {
    auto handler = (mode == HelloMode::SERVER ? serverModeHello : clientModeHello);
    return handler(*this, protocolVersions, finalVersion);
}


Status ProtocolConnection::hello(adl::int64_t version, HelloMode mode) {
    adl::ArrayList<adl::int64_t> arr;
    arr.append(version);
    adl::int64_t finalVer;
    return hello(arr, mode, &finalVer);
}


Status ProtocolConnection::sendHello(const adl::ArrayList<adl::int64_t>& protocolVersions) {
    auto data = protocolVersions;
    for (auto& it : data)
        it = adl::htonq(it);

    return sendMsg(MsgType::Hello, data.data(), data.size() * sizeof(adl::int64_t));
}



Status ProtocolConnection::recvHello(adl::ArrayList<adl::int64_t>& out) {
    Msg* msg;
    Status status = recvMsg(&msg, MsgType::Hello);
    if (status != Status::SUCCESS)
        return status;

    out.clear();

    for (size_t i = 0; i < msg->header.length / sizeof(int64_t); i++) {
        void* p = msg->data + i * sizeof(int64_t);
        adl::int64_t value = * (int64_t *) p;
        out.append( adl::ntohq(value) );
    }

    this->freeMsg(msg);

    return status;
}



}
