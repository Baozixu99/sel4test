/*
 * IPv4 Addr
 *
 * Created on 2025.1.20 at Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */

#pragma once

#include <adl/sys/types.h>
#include <adl/arpa/inet.h>
#include <monkey/Status.h>
#include <adl/TString.h>


namespace monkey::net {

class IP4Addr {
public:
    union {
        adl::uint32_t ui32;  // in net order
        adl::int32_t i32;  // in net order
        adl::uint8_t ui8arr[4];
        adl::int8_t i8arr[4];
    };

public:
    IP4Addr() { i32 = 0; }
    IP4Addr(adl::uint8_t ip[4]) { ui32 = * (adl::uint32_t*) ip; }
    IP4Addr(adl::int8_t ip[4]) { ui32 = * (adl::uint32_t*) ip; }
    IP4Addr(adl::uint32_t ip, bool netOrder = true) { ui32 = netOrder ? ip : adl::htonl(ip); }
    IP4Addr(adl::int32_t ip, bool netOrder = true) { i32 = netOrder ? ip : adl::htonl(ip); }

    adl::TString toString() const;
    monkey::Status set(const adl::TString&);  // like "192.168.1.1"

    bool operator == (const IP4Addr& other) const { return ui32 == other.ui32; }
    bool operator != (const IP4Addr& other) const { return ui32 != other.ui32; }
    bool operator <= (const IP4Addr& other) const { return ui32 <= other.ui32; }
    bool operator >= (const IP4Addr& other) const { return ui32 >= other.ui32; }
    bool operator < (const IP4Addr& other) const { return ui32 < other.ui32; }
    bool operator > (const IP4Addr& other) const { return ui32 > other.ui32; }

};


}
