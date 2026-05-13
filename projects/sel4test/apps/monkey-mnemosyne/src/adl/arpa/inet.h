// 124033910070 GTY
// gongty [at] tongji [dot] edu [dot] cn

// created on 2024.11.25
//   at Jiangchuan, Minhang, Shanghai

// implements: https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/arpa_inet.h.html

#pragma once

#include "../endian.h"

namespace adl {


inline int32_t htonl(int32_t x) {
    return htobe32(x);
}

inline uint32_t htonl(uint32_t x) {
    return htobe32(x);
}


inline uint16_t htons(uint16_t x) {
    return htobe16(x);
}


inline int64_t htonq(int64_t x) {
    return htobe64(x);
}


inline uint64_t htonq(uint64_t x) {
    return htobe64(x);
}


inline int32_t ntohl(int32_t x) {
    return htonl(x);
}

inline uint32_t ntohl(uint32_t x) {
    return htonl(x);
}

inline uint16_t ntohs(uint16_t x) {
    return htons(x);
}

inline int64_t ntohq(int64_t x) {
    return htonq(x);
}

inline uint64_t ntohq(uint64_t x) {
    return htonq(x);
}


}  // namespace adl
