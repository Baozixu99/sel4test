// SPDX-License-Identifier: MulanPSL-2.0

/*
 * 整数类型头文件。
 * 创建于 2022年7月2日。
 */

#pragma once

// forked from yros stdlib.
// github.com/FlowerBlackG/YurongOS

// modified for Amkos

namespace adl {

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

// 拥有最大宽度的整数类型。
typedef int64_t intmax_t;
typedef uint64_t uintmax_t;

#ifndef intptr_t
typedef int64_t intptr_t;
#endif

#ifndef uintptr_t
typedef uint64_t uintptr_t;
#endif

}


// https://github.com/openbsd/src/blob/master/sys/sys/stdint.h

/*
 * 7.18.2 Limits of specified-width integer types.
 *
 * The following object-like macros specify the minimum and maximum limits
 * of integer types corresponding to the typedef names defined above.
 */

/* 7.18.2.1 Limits of exact-width integer types */
#define	INT8_MIN		(-0x7f - 1)
#define	INT16_MIN		(-0x7fff - 1)
#define	INT32_MIN		(-0x7fffffff - 1)
#define	INT64_MIN		(-0x7fffffffffffffffLL - 1)

#define	INT8_MAX		0x7f
#define	INT16_MAX		0x7fff
#define	INT32_MAX		0x7fffffff
#define	INT64_MAX		0x7fffffffffffffffLL

#define	UINT8_MAX		0xff
#define	UINT16_MAX		0xffff
#define	UINT32_MAX		0xffffffffU
#define	UINT64_MAX		0xffffffffffffffffULL

/* 7.18.2.4 Limits of integer types capable of holding object pointers */
#ifdef __LP64__
#define	INTPTR_MIN		(-0x7fffffffffffffffL - 1)
#define	INTPTR_MAX		0x7fffffffffffffffL
#define	UINTPTR_MAX		0xffffffffffffffffUL
#else
#define	INTPTR_MIN		(-0x7fffffffL - 1)
#define	INTPTR_MAX		0x7fffffffL
#define	UINTPTR_MAX		0xffffffffUL
#endif

/* 7.18.2.5 Limits of greatest-width integer types */
#define	INTMAX_MIN		INT64_MIN
#define	INTMAX_MAX		INT64_MAX
#define	UINTMAX_MAX		UINT64_MAX

