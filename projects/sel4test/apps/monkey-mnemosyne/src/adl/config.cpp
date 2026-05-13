// SPDX-License-Identifier: MulanPSL-2.0

/*
 * 设置。
 *
 * 创建于 2023年7月2日 上海市嘉定区安亭镇
 */

// forked from yros stdlib.
// github.com/FlowerBlackG/YurongOS

// modified for Amkos

#include "./config.h"
#include "./sys/types.h"

namespace adl {

Allocator defaultAllocator;

}




// by Gemini 3.0 Pro

// -------------------------------------------------------------
// 全局 operator delete 重载
// -------------------------------------------------------------
// 因为编译选项使用了 -nostdlib，C++ 编译器生成的隐式析构调用
// 找不到标准库的 delete，必须手动链接到 libc 的 free。

extern "C" void free(void* ptr); // 声明 C 库的 free 函数

void operator delete(void* ptr) noexcept {
    if (ptr) {
        free(ptr);
    }
}

// 既然你在写 C++，为了保险起见，建议把 operator new 也加上，
// 否则万一哪里隐式用了 new 也会报错。
extern "C" void* malloc(unsigned long size);

void* operator new(unsigned long size) {
    return malloc(size);
}
