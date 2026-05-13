/*
 * Portable hexdump util for C/C++
 * 
 * gongty [at] tongji [dot] edu [dot] cn
 * Created on 2024.12.8 at Jiangchuan, Minhang, Shanghai
 * 
 * 
 * Usage:
 *   You can change ReadFunc and WriteFunc to any type you want.
 *   But you should ensure their prototype:
 *   - ReadFunc : Callable. Returning one character as int. EOF for end.
 *   - WriteFunc: Callable. Receive one int as character.
 */


#include <monkey/log.h>
#include <adl/sys/types.h>
#include <adl/TString.h>

namespace monkey::net::debug {

/**
 * @param num 
 * @param width Pass 0 for non-fixed width. 
 *              If num's hex width is wider, this argument is ignored.
 * @param upperCase Pass non-zero for upper-case. Pass zero for lower-case.
 */
template<typename WriteFunc>
static inline void writeHex(unsigned long num, int width, int upperCase, WriteFunc out) {
    int digitWidth = 1;
    
    {
        unsigned long needle = 0xF;
        while (needle < num) {
            needle <<= 4;
            needle |= 0xF;
            digitWidth ++;
        }

        if (width > 0) {
            for (int w = digitWidth; w < width; w++)
                out('0');
        }
    }


    while (digitWidth) {
        unsigned long digit = num >> ((digitWidth - 1) * 4);
        digit &= 0xF;
        if (digit >= 0 && digit <= 9) {
            out(digit + '0');
        } else {  // digit is in [a, f]
            out(digit - 0xA + (upperCase ? 'A' : 'a'));
        }

        digitWidth --;
    }

}


template<typename WriteFunc>
static inline void writeLine(int addr, int buf[], int len, int upperCase, WriteFunc out) {
    writeHex(addr, 8, upperCase, out);
    out(' ');
    out(' ');

    for (int i = 0; i < 16; i++) {
        if (i == 8) {
            out(i < len ? '-' : ' ');
            out(' ');
        }
        if (i < len) {
            writeHex(buf[i] & 0xFF, 2, upperCase, out);
        } else {
            out(' ');
            out(' ');
        }
        out(' ');
    }

    out(' ');
    out(' ');
    out(' ');
    out(' ');
    for (int i = 0; i < len; i++) {
        if (buf[i] >= 33 && buf[i] <= 126)
            out(buf[i]);
        else
            out('.');
    }

    out('\n');
}


template<typename ReadFunc, typename WriteFunc>
inline adl::size_t hexView(int upperCase, ReadFunc in, WriteFunc out) {
    int buf[16];
    int pos = 0;  // One after last occupied.

    adl::size_t addr = 0;
    adl::size_t bytesRead = 0;

    while (true) {
        int ch = in();
        if (ch == EOF)
            break;

        buf[pos++] = ch;
        if (pos == 16) {
            writeLine(addr, buf, 16, upperCase, out);
            addr += 16;
            pos = 0;
        }
    }


    if (pos)
        writeLine(addr, buf, pos, upperCase, out);
    return bytesRead;
}


inline adl::size_t hexView(const void* data, adl::size_t len) {
    adl::TString hex;
    adl::size_t count = 0;
    auto ret = hexView(
        true,
        [&] () {
            if (count < len)
                return (int) ((const char*) data)[count++];
            else
                return EOF;
        },
        [&] (int ch) {
            hex += char(ch);
        }
    );
    MONKEY_LOG_INFO(hex.c_str());
    return ret;
}


}
