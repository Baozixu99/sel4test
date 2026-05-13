// SPDX-License-Identifier: MulanPSL-2.0

/*
 * Array List Structure
 *   by gongty [AT] tongji [DOT] edu [DOT] cn
 *
 * Warning!! Heavy Data Structure:
 *   This structure heavily depends on template, each usage could make your kernel larger.
 *
 *
 * created on 2023.7.3 at Anting Town, Jiading, Shanghai
 */

/*
 * This data structure is forked from YurongOS, modified for Amkos.
 *
 * https://github.com/FlowerBlackG/YurongOS/blob/master/src/lib/collections/ArrayList.hpp
 */

#pragma once


#include "../string.h"
#include "../config.h"
#include "../stdint.h"
#include <adl/TString.h>

#include <stdio.h>

namespace adl {


template<typename DataType>
class ArrayListIterator {
    typedef ArrayListIterator Self;

protected:
    size_t size;
    size_t curr;
    DataType* data;

public:
    ArrayListIterator(DataType* data, size_t size, size_t curr) {
        this->size = size;
        this->data = data;
        this->curr = curr;
    }

    DataType& operator * () const { return data[curr]; };
    Self& operator ++ () {
        curr++;
        return *this;
    }

    Self operator ++ (int) {
        auto tmp = *this;
        curr++;
        return tmp;
    }

    friend bool operator == (const Self& a, const Self& b) {
        return a.curr == b.curr
            && a.size == b.size
            && a.data == b.data;
    }

    friend bool operator != (const Self& a, const Self& b) {
        return a.curr != b.curr
            || a.size != b.size
            || a.data != b.data;
    }

};


template<typename DataType>
class ArrayList {

protected:
    size_t _size = 0;
    size_t _capacity = 0;
    DataType* _data = nullptr;
    Allocator* allocator;

public:
    ArrayList() {
        this->allocator = &defaultAllocator;
    }

    ArrayList(Allocator& allocator) {
        this->allocator = &allocator;
    }


    ArrayList(const ArrayList<DataType>& other) {
        this->allocator = other.allocator;
        if (!this->resize(other.size())) {
            printf("failed to copy arraylist!\n");
            return;
        }

        adl::memcpy(this->_data, other._data, other._size * sizeof(DataType));
    }


    const ArrayList<DataType>& operator = (const ArrayList<DataType>& other) {
        if (this == &other) {
            return *this;
        }

        if (_capacity)
            allocator->free(_data);

        this->allocator = other.allocator;
        if (!this->resize(other.size())) {
            printf("failed to copy arraylist!\n");
            return *this;
        }

        adl::memcpy(this->_data, other._data, other._size * sizeof(DataType));
        return *this;
    }


    void clear() {
        _size = 0;
    }

    virtual ~ArrayList() {
        if (_capacity)
            allocator->free(_data);
    }

    inline size_t size() const {
        return this->_size;
    }

    inline size_t capacity() const {
        return this->_capacity;
    }

    inline DataType* data() {
        return this->_data;
    }

    inline const DataType* data() const {
        return this->_data;
    }

    void reserve(size_t new_capacity) {
        if (new_capacity <= _capacity) {
            // Do nothing
            return;
        }
        if (!this->allocator) { // Check null ptr
            printf("[CRITICAL] Allocator is null!\n");
            return;
        }
        // Allocate new memory
        auto newAddr = allocator->alloc<DataType>(new_capacity);
        if (!newAddr) {
            printf("[CRITICAL] ArrayList reserve failed: insufficient memory.\n");
            return;
        }
        // Copy the original data to new memory
        for (size_t i = 0; i < _size; i++)
            newAddr[i] = _data[i];
        // Free old memory
        if (_capacity) {
            allocator->free(_data);
        }
        // Update ptr and capacity
        _data = newAddr;
        _capacity = new_capacity;
    }


    bool resize(size_t newSize) {
        if (newSize > _size) {
            reserve(newSize);
            _size = _capacity;
            return _size == newSize;
        }
        else {
            _size = newSize;
            return true;
        }
    }

    int append(const DataType& data) {

        if (_size == _capacity) {
            // alloc more memory
            size_t newCapacity = _capacity;
            if (newCapacity == 0) {
                newCapacity = 16;
            } else {
                newCapacity += newCapacity / 2;
            }

            auto newAddr = allocator->alloc<DataType>(newCapacity);
            if (!newAddr) {
                return 1; // error
            }

            for (size_t i = 0; i < _size; i++)
                newAddr[i] = _data[i];

            if (_capacity)
                allocator->free(_data);

            _data = newAddr;
            _capacity = newCapacity;
        }

        _data[_size++] = data;

        return 0; // inserted successfully
    }

    inline int push(const DataType& data) {
        return this->append(data);
    }

    DataType pop() {
        if (_size) {
            return _data[--_size]; // todo: should call destructor
        }

        return * ( DataType* ) 0;
    }

    
    adl::size_t count(const DataType& data) const {
        adl::size_t res = 0;
        for (const auto& it : *this) {
            if (it == data) {
                res++;
            }
        }
        return res;
    }


    bool contains(const DataType& data) const {
        return count(data);
    }


    DataType& operator [] (size_t idx) {
        return _data[idx];
    }

    const DataType& operator [] (size_t idx) const {
        return _data[idx];
    }


    bool operator == (const ArrayList& other) const {
        if (_size != other._size)
            return false;
        for (adl::size_t i = 0; i < _size; i++) {
            if (_data[i] != other[i])
                return false;
        }
        return true;
    }


    DataType& back() { return _data[_size - 1]; }
    DataType& front() { return _data[0]; }


    bool isEmpty() const { return _size == 0; }
    bool isNotEmpty() const { return _size > 0; }

    
    // ------ iteration related ------

    ArrayListIterator<DataType> begin() const {
        return ArrayListIterator<DataType>(_data, _size, 0);
    }

    ArrayListIterator<DataType> end() const {
        return ArrayListIterator<DataType>(_data, _size, _size);
    }

};


class ByteArray : public ArrayList<adl::uint8_t> {
protected:
    int construct(const void* data, adl::size_t dataLen, adl::Allocator& alloc) {
        this->allocator = &alloc;
        if (!resize(dataLen)) {
            // failed to reserve.
            printf("Failed to initialize ByteArray! No memory.\n");
            return -1;
        }
        adl::memcpy(this->_data, data, dataLen);
        return 0;
    }

public:
    ByteArray() : adl::ArrayList<adl::uint8_t>() {}

    ByteArray(adl::Allocator& alloc) : adl::ArrayList<adl::uint8_t>(alloc) {}


    ByteArray(const void* data, adl::size_t dataLen, adl::Allocator& alloc = defaultAllocator)
    : adl::ArrayList<adl::uint8_t>(alloc)
    {
        construct(data, dataLen, alloc);
    }


    ByteArray(const char* str, adl::Allocator& alloc = defaultAllocator)
    : adl::ArrayList<adl::uint8_t>(alloc) 
    {
        construct(str, (str ? strlen(str) : 0), alloc);
    }


    ByteArray(const TString& str, adl::Allocator& alloc = defaultAllocator) 
    : adl::ArrayList<adl::uint8_t>(alloc)
    {
        construct(str.data(), str.length(), alloc);
    }


    TString toString() const {
        TString str;
        for (auto& ch : *this) {
            str += char(ch);
        }

        return str;
    }


    int append(const void* data, adl::size_t len) {
        if (len == 0)
            return 0;

        if (!resize(_size + len))
            return 1;  // out of resource.

        adl::memcpy(_data + _size - len, data, len);

        return 0;
    }

};



}  // namespace adl
