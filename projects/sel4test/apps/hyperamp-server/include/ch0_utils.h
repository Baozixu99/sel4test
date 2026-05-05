/*
 * CH0 辅助函数 — 安全内存操作和缓存操作
 *
 * 从 hyperamp-server 原始头文件提取，供 CH0 模块独立使用。
 * 这些函数在 front 的 hyperamp_shm_queue.h 中未导出，
 * 因此需要在此处独立定义。
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CH0_UTILS_H
#define CH0_UTILS_H

#include <stdint.h>
#include <stddef.h>

/* 内存屏障 */
#if defined(__aarch64__) || defined(__arm__)
    #define CH0_BARRIER()   __asm__ volatile("dmb sy" ::: "memory")
#else
    #define CH0_BARRIER()   __asm__ volatile("mfence" ::: "memory")
#endif

/* 安全 memset */
static inline void hyperamp_safe_memset(volatile void *dst, uint8_t val, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (size_t i = 0; i < len; i++) {
        p[i] = val;
    }
    CH0_BARRIER();
}

/* 安全 memcpy */
static inline void hyperamp_safe_memcpy(volatile void *dst, const volatile void *src, size_t len)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    CH0_BARRIER();
}

/* 安全读取 u16 */
static inline uint16_t hyperamp_safe_read_u16(const volatile void *addr, size_t offset)
{
    const volatile uint8_t *p = (const volatile uint8_t *)addr;
    uint16_t val = 0;
    for (int i = 0; i < 2; i++) {
        val |= ((uint16_t)p[offset + i]) << (i * 8);
    }
    CH0_BARRIER();
    return val;
}

/* 安全读取 u32 */
static inline uint32_t hyperamp_safe_read_u32(const volatile void *addr, size_t offset)
{
    const volatile uint8_t *p = (const volatile uint8_t *)addr;
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
        val |= ((uint32_t)p[offset + i]) << (i * 8);
    }
    CH0_BARRIER();
    return val;
}

#endif /* CH0_UTILS_H */
