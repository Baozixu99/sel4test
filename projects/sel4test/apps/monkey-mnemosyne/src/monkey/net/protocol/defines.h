/*
 * Basic type defines
 *
 * Created on 2025.1.21 at Xiangzhou, Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */

#pragma once

#include <adl/sys/types.h>
#include <adl/string.h>
#include <adl/TString.h>
#include <adl/arpa/inet.h>

#include <adl/Allocator.h>
#include <adl/collections/ArrayList.hpp>

#include <monkey/Status.h>
#include <monkey/net/TcpIo.h>


#define __packed __attribute__((__packed__))



namespace monkey::net::protocol {


extern const char* MAGIC;
const adl::size_t MAGIC_LEN = 4;


enum class MsgType : adl::uint32_t {
    None = 0x0000,

    Response = 0xA001,

    Hello = 0x1000,
    Auth = 0x1001,

    GetIdentityKeys = 0x1100,

    MemoryNodeShowId = 0x2000,
    MemoryNodeClockIn = 0x2001,
    MemoryNodeClockOut = 0x2002,
    MemoryNodeHandover = 0x2003,
    LocateMemoryNodes = 0x2004,

    TryAlloc = 0x3001,
    ReadBlock = 0x3002,
    WriteBlock = 0x3003,
    CheckAvailMem = 0x3004,
    FreeBlock = 0x3005,
    RefBlock = 0x3006,
    UnrefBlock = 0x3007,
    GetBlockDataVersion = 0x3008,

    PingPong = 0x4001
};


const char* msgTypeToString(MsgType);


struct Header {
    union {
        adl::uint8_t magic[4];

        // You can treat it as 32-bit integer.
        adl::uint32_t magicI32 = [] () {return * (adl::uint32_t*) MAGIC;} ();  
    } __packed;

    adl::uint32_t type;
    adl::uint64_t length;  // Tell byte-order whenever use.
} __packed;


/**
 * Common message.
 */
struct Msg {
    Header header;
    adl::uint8_t data[0];
} __packed;


struct Response {
    Header header;
    adl::uint32_t code;
    adl::uint32_t msgLen;

    const char msg[0];  // Not null-terminated for error.
} __packed;


}
