/* 2051565 GTY Tongji CS */
// Ported from TJ OOP HW. Modified for Amkos.

#define _CRT_SECURE_NO_WARNINGS


#include <adl/Allocator.h>

#include <adl/TString.h>
#include <adl/string.h>
#include <adl/config.h>

#include <adl/collections/ArrayList.hpp>

using namespace std;


static void __die() {
    printf("[FATAL] Panic. ");
    printf(__FILE__);
    printf("\n");
    while (1)
        ;
}


namespace adl {


TString::TString(adl::Allocator* alloc)
{
    this->allocator = alloc;
    content = nullptr;
    len = 0;
}

TString::TString(const char* str, adl::Allocator* alloc)
{
    this->allocator = alloc;

    if (str == nullptr || str[0] == '\0') {
        content = nullptr;
        len = 0;
    }
    else {
        len = adl::strlen(str);

        content = alloc->alloc<char>(len + 1, false);

        if (content == nullptr) {
            __die();
        }


        adl::strcpy(content, str);
    }
}

#if defined(__GNUC__)

#else
TString::TString(const int x)
{
    if (x == 0) { // x == NULL
        len = 0;
        content = nullptr;
    }
    else {
        len = 0;
        content = nullptr;
    }
}
#endif

TString::TString(const TString& str, adl::Allocator* alloc)
{
    this->allocator = alloc;

    if (str.length() == 0) {
        content = nullptr;
        len = 0;
    }
    else {
        len = str.length();
        content = alloc->alloc<char>(len + 1, false);
        if (content == nullptr) {
            __die();
        }

        adl::strcpy(content, str.c_str());
    }
}

TString::~TString()
{
    freeUp();
}

void TString::freeUp()
{
    if (content != nullptr) {
        freeUpContent();
        len = 0;
    }
}


void TString::clear() {
    freeUp();
}


void TString::freeUpContent()
{
    if (content != nullptr) {
        allocator->free(content);
        content = nullptr;
    }
}

size_t TString::length() const
{
    return len;
}

const char* TString::c_str() const
{
    return content;
}

TString& TString::operator=(const TString& str)
{
    if (&str == this) {
        return *this;
    }

    freeUp();

    if (str.length() == 0) {
        return *this;
    }
    else {
        content = allocator->alloc<char>(str.length() + 1, false);
        if (content == nullptr) {
            __die();
        }

        adl::strcpy(content, str.c_str());
        len = str.length();
        return *this;
    }
}

TString& TString::operator=(const char* str)
{
    if (str == this->content) {
        return *this;
    }

    freeUp();

    if (str == nullptr || str[0] == '\0') {
        return *this;
    }
    else {
        len = strlen(str);
        content = allocator->alloc<char>(len + 1, false);
        if (content == nullptr) {
            __die();
        }

        strcpy(content, str);
        return *this;
    }
}

const TString TString::operator+(const TString& str) const
{
    TString ret;

    ret.len = this->len + str.len;

    if (ret.len == 0) {
        return ret;
    }

    ret.content = allocator->alloc<char>(ret.len + 1, false);
    if (ret.content == nullptr) {
        __die();
    }


    if (this->content != nullptr) {
        adl::memcpy(ret.content, this->content, this->len * sizeof(char));
    }
    if (str.c_str() != nullptr) {
        adl::memcpy(ret.content + this->len, str.c_str(), str.length() * sizeof(char));
    }
    ret.content[ret.len] = '\0';

    return ret;
}

const TString TString::operator+(const char* str) const
{
    TString ret;

    auto lengthOfStr = (str == nullptr ? 0 : adl::strlen(str));

    ret.len = this->len + lengthOfStr;

    if (ret.len == 0) {
        return ret;
    }

    ret.content = allocator->alloc<char>(ret.len + 1, false);
    if (ret.content == nullptr) {
        __die();
    }

    if (this->content != nullptr) {
        adl::memcpy(ret.content, this->content, this->len * sizeof(char));
    }

    adl::memcpy(ret.content + this->len, str, lengthOfStr * sizeof(char));
    ret.content[ret.len] = '\0';

    return ret;
}

const TString operator+(const char* strA, const TString& strB)
{
    TString ret {strB.allocator};

    auto lengthOfStrA = (strA == nullptr ? 0 : adl::strlen(strA));
    ret.len = lengthOfStrA + strB.length();

    if (ret.len == 0) {
        return ret;
    }

    ret.content = ret.allocator->alloc<char>(ret.len + 1, false);
    if (ret.content == nullptr) {
        __die();
    }

    adl::memcpy(ret.content, strA, lengthOfStrA * sizeof(char));
    adl::memcpy(ret.content + lengthOfStrA, strB.c_str(), strB.length() * sizeof(char));
    ret.content[ret.len] = '\0';

    return ret;
}

const TString TString::operator+(const char c) const
{
    char tmp[2] = { c, '\0' };
    return *this + tmp;
}

const TString TString::operator!() const
{
    TString ret = *this;
    if (ret.length() >= 2) {
        char c;
        for (size_t i = 0; i < ret.length() / 2; i++) {
            c = ret[i];
            ret[i] = ret[ret.length() - 1 - i];
            ret[ret.length() - 1 - i] = c;
        }
    }

    return ret;

}

const TString TString::operator-(const TString& str) const
{
    if (str.len == 0 || this->len == 0) {
        return *this;
    }
    else {
        char* occurenceLocation = adl::strstr(content, str.content);
        if (occurenceLocation == nullptr) {
            return *this;
        }
        else if (len == str.length()) {
            return TString(this->allocator);
        }
        else {
            char* p = allocator->alloc<char>(len - str.length() + 1, false);
            if (p == nullptr) {
                __die();
            }
            TString ret {this->allocator};
            ret.len = len - str.length();
            ret.content = p;

            adl::memcpy(p, content, adl::uint64_t(occurenceLocation - content) * sizeof(char));
            adl::memcpy(
                p + (occurenceLocation - content),
                occurenceLocation + str.length(),
                (len - adl::uint64_t(occurenceLocation - content) - str.length()) * sizeof(char)
            );
            p[ret.len] = '\0';

            return ret;
        }
    }
}

const TString TString::operator-(const char* str) const
{
    if (str == nullptr || str[0] == '\0' || this->len == 0) {
        return *this;
    }
    else {
        auto lengthOfStr = adl::strlen(str);

        char* occurenceLocation = adl::strstr(content, str);
        if (occurenceLocation == nullptr) {
            return *this;
        }
        else if (len == lengthOfStr) {
            return TString(this->allocator);
        }
        else {
            char* p = allocator->alloc<char>(len - lengthOfStr + 1);
            if (p == nullptr) {
                __die();
            }
            TString ret;
            ret.len = len - lengthOfStr;
            ret.content = p;

            adl::memcpy(p, content, adl::uint64_t(occurenceLocation - content) * sizeof(char));
            adl::memcpy(
                p + (occurenceLocation - content),
                occurenceLocation + lengthOfStr,
                (len - adl::uint64_t(occurenceLocation - content) - lengthOfStr) * sizeof(char)
            );
            p[ret.len] = '\0';

            return ret;
        }
    }
}

const TString TString::operator - (const char c) const
{
    if (c == '\0' || len == 0) {
        return *this;
    }
    else {
        for (size_t i = 0; i < len; i++) {
            if (c == content[i]) {
                TString ret{this->allocator};
                ret.len = len - 1;
                ret.content = allocator->alloc<char>(len, false);
                if (ret.content == nullptr) {
                    __die();
                }

                adl::memcpy(ret.content, content, i * sizeof(char));
                adl::memcpy(ret.content + i, content + i + 1, (len - i - 1) * sizeof(char));
                ret[ret.len] = '\0';
                return ret;
            }
        }

        return *this;
    }
}

TString& TString::operator+=(const TString& str)
{
    if (str.len == 0) {
        return *this;
    }
    else {
        char* p;
        auto resLen = len + str.len;

        p = allocator->alloc<char>(resLen + 1, false);
        if (p == nullptr) {
            __die();
        }
        if (content != nullptr) {
            adl::memcpy(p, content, len * sizeof(char));

            this->freeUpContent();
        }

        adl::memcpy(p + len, str.content, str.len * sizeof(char));
        p[resLen] = '\0';

        this->len = resLen;

        this->content = p;
        return *this;
    }
}
TString& TString::operator+=(const char* str)
{
    if (str == nullptr || str[0] == '\0') {
        return *this;
    }
    else {
        auto lengthOfStr = adl::strlen(str);
        auto resLen = len + lengthOfStr;

        char* p;

        p = allocator->alloc<char>(resLen + 1, false);
        if (p == nullptr) {
            __die();
        }
        if (content != nullptr) {
            adl::memcpy(p, content, len * sizeof(char));
            this->freeUpContent();
        }

        adl::memcpy(p + len, str, lengthOfStr * sizeof(char));
        p[resLen] = '\0';

        this->len = resLen;

        this->content = p;
        return *this;
    }
}
TString& TString::operator+=(const char c)
{
    if (c == '\0') {
        return *this;
    }
    else {
        auto resLen = len + 1;
        char* p;

        p = allocator->alloc<char>(resLen + 1, false);
        if (p == nullptr) {
            __die();
        }

        if (this->content != nullptr) {
            adl::memcpy(p, content, len * sizeof(char));
            this->freeUpContent();
        }

        p[resLen - 1] = c;
        p[resLen] = '\0';

        this->len = resLen;
        this->content = p;
        return *this;
    }
}

TString& TString::operator-=(const TString& str)
{
    return *this = *this - str;
}
TString& TString::operator-=(const char* str)
{
    return *this = *this - str;
}
TString& TString::operator-=(const char c)
{
    return *this = *this - c;
}

char& TString::operator[](size_t i)
{
    return content[i];
}

const char& TString::operator[](size_t i) const
{
    return content[i];
}


const TString TString::operator*(int x) const
{
    TString ret;

    if (this->len == 0 || x <= 0) {
        return ret;
    }
    else {
        // x >= 1
        char* p = allocator->alloc<char>(len * adl::size_t(x) + 1, false);
        if (p == nullptr) {
            __die();
        }

        for (adl::size_t i = 0; i < adl::size_t(x); i++) {
            adl::memcpy(p + i * len, content, len * sizeof(char));
        }

        ret.content = p;
        ret.len = this->len * adl::size_t(x);
        ret.content[ret.len] = '\0';

        return ret;
    }
}

const TString operator*(const int x, const TString& str)
{
    return str * x;
}

TString& TString::operator*=(int x)
{
    return *this = *this * x;
}

const TString operator+(const char c, const TString& str)
{
    char tmp[2] = { c, '\0' };
    return tmp + str;
}

bool TString::operator==(const TString& str) const
{
    if (this->content == str.content) {
        return true;
    }
    else if (this->len != str.len) {
        return false;
    }
    else if (this->content == nullptr || str.content == nullptr) {
        return false;
    }
    else {
        for (size_t i = 0; i < len; i++) {
            if (this->content[i] != str[i]) {
                return false;
            }
        }
        return true;
    }
}

bool TString::operator==(const char* str) const
{
    if (str == content) {
        return true;
    }
    else if (str == nullptr || str[0] == '\0') {
        return len == 0;
    }
    else if (len == 0) {
        return false;
    }

    auto lenOfStr = adl::strlen(str);
    if (lenOfStr != len) {
        return false;
    }
    for (size_t i = 0; i < lenOfStr; i++) {
        if (str[i] != content[i]) {
            return false;
        }
    }
    return true;
}

bool TString::operator!=(const char* str) const
{
    return !(*this == str);
}

bool TString::operator!=(const TString& str) const
{
    return !(*this == str);
}

bool TString::operator>(const TString& str) const
{
    if (this->len + str.len == 0 || this->len == 0) {
        return false;
    }
    else if (str.len == 0) {
        return true;
    }
    else {
        auto minLen = len < str.len ? len : str.len;
        for (size_t i = 0; i < minLen; i++) {
            if (content[i] > str[i]) {
                return true;
            }
            else if (content[i] < str[i]) {
                return false;
            }
        }

        return len > str.len;
    }
}

bool TString::operator>(const char* str) const
{
    auto lengthOfStr = (str == nullptr ? 0 : adl::strlen(str));
    if (this->len + lengthOfStr == 0 || this->len == 0) {
        return false;
    }
    else if (lengthOfStr == 0) {
        return true;
    }
    else {
        auto minLen = len < lengthOfStr ? len : lengthOfStr;
        for (size_t i = 0; i < minLen; i++) {
            if (content[i] > str[i]) {
                return true;
            }
            else if (content[i] < str[i]) {
                return false;
            }
        }

        return len > lengthOfStr;
    }
}


bool TString::operator<(const TString& str) const
{
    if (this->len + str.len == 0 || str.len == 0) {
        return false;
    }
    else if (len == 0) {
        return true;
    }
    else {
        auto minLen = len < str.len ? len : str.len;
        for (size_t i = 0; i < minLen; i++) {
            if (content[i] < str[i]) {
                return true;
            }
            else if (content[i] > str[i]) {
                return false;
            }
        }

        return len < str.len;
    }
}

bool TString::operator<(const char* str) const
{
    auto lengthOfStr = (str == nullptr ? 0 : adl::strlen(str));
    if (this->len + lengthOfStr == 0 || lengthOfStr == 0) {
        return false;
    }
    else if (len == 0) {
        return true;
    }
    else {
        auto minLen = len < lengthOfStr ? len : lengthOfStr;
        for (size_t i = 0; i < minLen; i++) {
            if (content[i] < str[i]) {
                return true;
            }
            else if (content[i] > str[i]) {
                return false;
            }
        }

        return len < lengthOfStr;
    }
}

bool TString::operator>=(const TString& str) const
{
    return !(*this < str);
}
bool TString::operator>=(const char* str) const
{
    return !(*this < str);
}

bool TString::operator<=(const TString& str) const
{
    return !(*this > str);
}
bool TString::operator<=(const char* str) const
{
    return !(*this > str);
}


TString TString::substr(const size_t pos, const size_t len) const {
    TString ret;

    if (len == 0 || pos >= this->len)
        return ret;

    size_t endIdx = pos + len;  // exclusive
    if (endIdx > this->len) {
        endIdx = this->len;
    }

    ret.len = endIdx - pos;
    ret.content = allocator->alloc<char>(ret.len + 1, false);
    if (ret.content == nullptr) {
        __die();
    }

    adl::memcpy(ret.content, this->content + pos, ret.len);
    ret.content[ret.len] = '\0';

    return ret;
}



void TString::split(const TString& key, ArrayList<TString>& out) const {
    out.clear();
    if (key.length() == 0) {
        out.append(key);
        return;
    }

    adl::TString tmp{this->allocator};
    adl::size_t i = 0;
    while (i < length()) {
        if (adl::strncmp(key.c_str(), content + i, key.length()) == 0) {
            if (tmp.length()) {
                out.append(tmp);
                tmp.clear();
            }

            i += key.length();
            continue;
        }

        tmp += content[i++];
    }

    if (tmp.length()) {
        out.append(tmp);   
    }
}


bool operator>(const char* strA, const TString& strB)
{
    return strB < strA;
}
bool operator<(const char* strA, const TString& strB)
{
    return strB > strA;
}
bool operator>=(const char* strA, const TString& strB)
{
    return strB <= strA;
}
bool operator<=(const char* strA, const TString& strB)
{
    return strB >= strA;
}


bool operator==(const char* strA, const TString& strB)
{
    return strB == strA;
}

bool operator!=(const char* strA, const TString& strB)
{
    return strB != strA;
}


int64_t TString::toInt64() const {
    if (len == 0)
        return 0;

    bool negative = false;
    int64_t res = 0;
    size_t idx = 0;
    if (content[0] == '-') {
        negative = true;
        idx++;
    }

    while (idx < len) {
        char ch = content[idx];
        
        if (ch < '0' || ch > '9')
            break;

        res *= 10;
        res += ch - '0';
    
        idx++;
    }

    return negative ? (-res) : res;
}


#define TSTRING_IMPL_TO_STRING(Type) \
    TString TString::to_string(const Type x) { \
        TString res; \
        Type value = x; \
        while (value) { \
            res = char((value % 10) + '0') + res; \
            value /= 10; \
        } \
\
        return res.length() ? res : "0"; \
    }


TSTRING_IMPL_TO_STRING(adl::uint32_t)
TSTRING_IMPL_TO_STRING(adl::uint64_t)
TSTRING_IMPL_TO_STRING(adl::size_t)


#undef TSTRING_IMPL_TO_STRING


}  // namespace adl
