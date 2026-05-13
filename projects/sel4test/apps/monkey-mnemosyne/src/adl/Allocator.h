// SPDX-License-Identifier: MulanPSL-2.0

/*
 * 分配器。
 *
 * 创建于 2023年7月2日 上海市嘉定区安亭镇
 */

// forked from yros stdlib.
// github.com/FlowerBlackG/YurongOS

// modified for Amkos

#pragma once

#include "./sys/types.h"

namespace adl { struct Allocator; };


// DO NOT call this outside adl::Allocator!
inline void* operator new(adl::size_t, adl::Allocator* addr) {
    return addr;
}


namespace adl {


struct Allocator {
public:
    typedef void* (* AllocFunc) (size_t, void*);
    typedef void (* FreeFunc) (void*, size_t, void*);

protected:
    void* data = nullptr;
    AllocFunc _alloc = nullptr;
    FreeFunc _free = nullptr;

    struct Header {
        size_t size;
        size_t count;
    };


public:

    template<typename T>
    inline T* allocNoConstruct(size_t count = 1, size_t objSize = sizeof(T)) {
        if (count == 0)
            return nullptr;

        uintptr_t addr = (uintptr_t) this->_alloc(objSize * count + sizeof(Header), data);

        if (!addr)
            return nullptr;

        auto header = (Header*) addr;
        header->size = objSize;
        header->count = count;

        auto objs = (T*) (addr + sizeof(Header));

        return objs;
    }


    template<typename T>
    inline T* alloc(size_t count = 1, bool construct = true, size_t objSize = sizeof(T)) {

        auto objs = allocNoConstruct<T>(count, objSize);
        if (!objs)
            return nullptr;

        if (construct) {
            for (size_t i = 0; i < count; i++)
                // redirect to fake allocation that doesn't give any extra memory.
                new ((Allocator*) (objs + i)) T;  
        }

        return objs;
    }


    inline void free(void* addr) {
        auto header = (Header*) (uintptr_t(addr) - sizeof(Header));
        this->_free(header, header->count * header->size + sizeof(Header), data); 
    }

    
    template<typename T>
    inline void free(T* addr, bool destruct = true) {
        auto header = (Header*) (uintptr_t(addr) - sizeof(Header));

        if (destruct) {
            size_t i = header->count - 1;
            do {
                uintptr_t objAddr = uintptr_t(addr) + i * header->size;
                ((T*) objAddr)->~T();
            } while (i--);
        }

        this->_free(header, header->count * header->size + sizeof(Header), data); 
    }


    inline bool ready() { return !!_alloc && !!_free; }
    inline bool notReady() { return !ready(); }

    inline void bindAlloc(AllocFunc func) { this->_alloc = func; };
    inline void bindFree(FreeFunc func) { this->_free = func; };
    inline void bindData(void* data) { this->data = data; };


    void init(AllocFunc alloc, FreeFunc free, void* data = nullptr) {
        bindAlloc(alloc);
        bindFree(free);
        bindData(data);
    }

    struct InitParams {
        AllocFunc alloc;
        FreeFunc free;
        void* data = nullptr;
    };

    Allocator() = default;

    Allocator(AllocFunc alloc, FreeFunc free, void* data = nullptr) { init(alloc, free, data); }
    Allocator(const InitParams& x) { init(x.alloc, x.free, x.data); }

    inline void init(InitParams x) { init(x.alloc, x.free, x.data); }

    inline Allocator operator = (InitParams x) { init(x); return *this; }

};




}  // namespace adl

