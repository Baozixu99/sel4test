// Mock Genode::log
// by Gemini 3.0 Pro

#pragma once

#include <stdio.h>

namespace Genode {

    // --- 1. Helper overloads ---

    // Signed integers
    inline void print_arg(int v)                { printf("%d", v); }
    inline void print_arg(long v)               { printf("%ld", v); }
    inline void print_arg(long long v)          { printf("%lld", v); }

    // Unsigned integers (Fixes "ambiguous overload" errors)
    inline void print_arg(unsigned int v)       { printf("%u", v); }
    inline void print_arg(unsigned long v)      { printf("%lu", v); }
    inline void print_arg(unsigned long long v) { printf("%llu", v); }

    // Pointers and Strings
    inline void print_arg(const char* v)        { printf("%s", v); }
    inline void print_arg(void* v)              { printf("%p", v); }

    // --- 2. Base cases (Fixes "no matching function" errors) ---

    inline void log() {
        printf("\n");
    }

    inline void error() {
        printf("\n");
    }

    inline void warning() {
        printf("\n");
    }

    // --- 3. Recursive variadic templates ---

    template <typename T, typename... Args>
    inline void log(T value, Args... args) {
        print_arg(value); 
        log(args...);     // Recurse
    }

    template <typename T, typename... Args>
    inline void error(T value, Args... args) {
        print_arg(value); 
        error(args...);   // Recurse
    }

    template <typename T, typename... Args>
    inline void warning(T value, Args... args) {
        print_arg(value); 
        warning(args...); // Recurse
    }

}