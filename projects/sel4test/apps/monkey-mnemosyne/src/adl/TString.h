/* 2051565 GTY Tongji CS */
// Ported from TJ OOP HW. Modified for Amkos.

#pragma once

#include "./Allocator.h"
#include "./config.h"

namespace adl {


template<typename T> class ArrayList;


class TString {
protected:
    char* content;
    size_t   len;
    adl::Allocator* allocator = nullptr;

    inline void freeUpContent();


public:
    static const size_t npos = -1;

    TString(adl::Allocator* alloc = &defaultAllocator);
    TString(const char* str, adl::Allocator* alloc = &defaultAllocator);
    TString(const TString& str, adl::Allocator* alloc = &defaultAllocator);

#if defined(__GNUC__)

#else
    TString(const int x);
#endif

    virtual ~TString();


    void freeUp();
    void clear();

    size_t length() const;
    const char* c_str() const;
    inline char* data() { return this->content; }
    inline const char* data() const { return this->content; }

    TString& operator = (const TString& str);
    TString& operator = (const char* str);

    const TString operator + (const TString& str) const;
    const TString operator + (const char* str) const;
    const TString operator + (const char c) const;

    const TString operator - (const TString& str) const;
    const TString operator - (const char* str) const;
    const TString operator - (const char c) const;

    const TString operator * (const int x) const;

    const TString operator ! () const;

    TString& operator *= (int x);
    TString& operator += (const TString& str);
    TString& operator += (const char* str);
    TString& operator += (const char c);

    TString& operator -= (const TString& str);
    TString& operator -= (const char* str);
    TString& operator -= (const char c);

    bool operator == (const TString& str) const;
    bool operator == (const char* str) const;
    bool operator != (const TString& str) const;
    bool operator != (const char* str) const;
    bool operator > (const TString& str) const;
    bool operator > (const char* str) const;
    bool operator < (const TString& str) const;
    bool operator < (const char* str) const;
    bool operator >= (const TString& str) const;
    bool operator >= (const char* str) const;
    bool operator <= (const TString& str) const;
    bool operator <= (const char* str) const;

    TString substr(const size_t pos, const size_t len = npos) const;
    char& at(const int n);
    const char& at(const int n) const;

    void split(const adl::TString&, adl::ArrayList<adl::TString>& out) const;

    char& operator[] (size_t i);
    const char& operator[] (size_t i) const;

    friend const TString operator + (const char* strA, const TString& strB);

    friend const TString operator + (const char c, const TString& str);
    friend const TString operator * (const int x, const TString& str);

    friend bool operator == (const char* strA, const TString& strB);
    friend bool operator != (const char* strA, const TString& strB);
    friend bool operator > (const char* strA, const TString& strB);
    friend bool operator < (const char* strA, const TString& strB);
    friend bool operator >= (const char* strA, const TString& strB);
    friend bool operator <= (const char* strA, const TString& strB);

    int64_t toInt64() const;

    static TString to_string(const adl::uint32_t);
    static TString to_string(const adl::uint64_t);
    static TString to_string(const unsigned long);
};



}  // namespace MtsysKv

