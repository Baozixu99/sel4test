// 124033910070 GTY
// gongty [at] tongji [dot] edu [dot] cn

// created on 2024.11.25
//   at Jiangchuan, Minhang, Shanghai

// implements: https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/endian.h.html


#pragma once

#include "./stdint.h"


#ifndef __BYTE_ORDER__
    #error "__BYTE_ORDER__ undefined. Compiler you are using is not supported by us."
#endif

#ifndef BYTE_ORDER
    #define BYTE_ORDER __BYTE_ORDER__
#endif

#ifndef BIG_ENDIAN
    #define BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif

#ifndef LITTLE_ENDIAN
    #define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#endif

#if ((BYTE_ORDER != BIG_ENDIAN) && (BYTE_ORDER != LITTLE_ENDIAN))
    #error "Strange system. Our system won't work on your device."
#endif


#if defined(_BYTEORDER_FUNC_DEFINED)
#error "Byteorder functions defined elsewhere. " \
    "You should include this header before any other."
#endif

#define _BYTEORDER_FUNC_DEFINED


namespace adl {


inline uint16_t be16toh(uint16_t x) {
#if BYTE_ORDER == BIG_ENDIAN
    return x;
#else  // BYTE_ORDER == LITTLE_ENDIAN
    return __builtin_bswap16(x);
#endif
}


inline uint32_t be32toh(uint32_t x) {
#if BYTE_ORDER == BIG_ENDIAN
    return x;
#else  // BYTE_ORDER == LITTLE_ENDIAN
    return __builtin_bswap32(x);
#endif

}
inline int32_t be32toh(int32_t x) {
    return (int32_t) be32toh((uint32_t) x);
}


inline uint64_t be64toh(uint64_t x) {
#if BYTE_ORDER == BIG_ENDIAN
    return x;
#else  // BYTE_ORDER == LITTLE_ENDIAN
    return __builtin_bswap64(x);
#endif

}


inline int64_t be64toh(int64_t x) {
    return (int64_t) be64toh((uint64_t) x);
}



inline uint16_t htobe16(uint16_t x) {
    return be16toh(x);
}
inline int32_t htobe32(int32_t x) {
    return be32toh(x);
}
inline uint32_t htobe32(uint32_t x) {
    return be32toh(x);
}
inline int64_t htobe64(int64_t x) {
    return be64toh(x);
}
inline uint64_t htobe64(uint64_t x) {
    return be64toh(x);
}



inline uint16_t htole16(uint16_t x) {
#if BYTE_ORDER == BIG_ENDIAN
    return __builtin_bswap16(x);
#else  // BYTE_ORDER == LITTLE_ENDIAN
    return x;
#endif

}


inline uint32_t htole32(uint32_t x) {
#if BYTE_ORDER == BIG_ENDIAN
    return __builtin_bswap32(x);
#else  // BYTE_ORDER == LITTLE_ENDIAN
    return x;
#endif
}


inline uint64_t htole64(uint64_t x) {
#if BYTE_ORDER == BIG_ENDIAN
    return __builtin_bswap64(x);
#else  // BYTE_ORDER == LITTLE_ENDIAN
    return x;
#endif
}


inline uint16_t le16toh(uint16_t x) {
    return htole16(x);
}

inline uint32_t le32toh(uint32_t x) {
    return htole32(x);
}

inline uint64_t le64toh(uint64_t x) {
    return htole64(x);
}


}  // namespace adl
