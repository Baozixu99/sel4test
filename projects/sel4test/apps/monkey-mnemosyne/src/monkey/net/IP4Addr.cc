/*
 * IPv4 Addr
 *
 * Created on 2025.1.20 at Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */

#include <monkey/net/IP4Addr.h>
#include <adl/TString.h>
#include <adl/collections/ArrayList.hpp>

using namespace adl;

namespace monkey::net {


TString IP4Addr::toString() const {
    adl::TString ipStr;
    
    ipStr += adl::TString::to_string((adl::uint32_t) ui8arr[0]);    
    ipStr += '.';
    ipStr += adl::TString::to_string((adl::uint32_t) ui8arr[1]);
    ipStr += '.';
    ipStr += adl::TString::to_string((adl::uint32_t) ui8arr[2]);
    ipStr += '.';
    ipStr += adl::TString::to_string((adl::uint32_t) ui8arr[3]);
    return ipStr;
}


monkey::Status IP4Addr::set(const adl::TString& str) {
    ArrayList<TString> segments;
    str.split(".", segments);
    
    if (segments.size() != 4)
        return Status::INVALID_PARAMETERS;

    for (adl::size_t i = 0; i < 4; i++) {
        
        // todo: check errors like "192.16aaa.1.1"

        ui8arr[i] = (adl::uint8_t) segments[i].toInt64();
    }

    return Status::SUCCESS;
}



}
