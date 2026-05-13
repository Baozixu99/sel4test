/*

    RC4


    Created on 2025.1.6 at Jiangchuan, Minhang, Shanghai

    gongty  [at] tongji [dot] edu [dot] cn
    feng.yt [at]  sjtu  [dot] edu [dot] cn

  
    Reference:
        https://blog.csdn.net/weixin_45582916/article/details/121429688
    
*/

#include <adl/collections/ArrayList.hpp>
#include <monkey/crypto/rc4.h>


using namespace adl;


static void rc4Init(unsigned char* s, const ByteArray& key) {
    int j = 0;
    char k[256] = { 0 };
    unsigned char tmp = 0;
    for (unsigned int i = 0; i < 256; i++) {
        s[i] = (unsigned char) i;
        k[i] = (char) key[i % key.size()];
    }
    for (unsigned int i = 0; i < 256; i++) {
        j = (j + s[i] + k[i]) % 256;
        tmp = s[i];
        s[i] = s[j];
        s[j] = tmp;
    }
}


namespace monkey::crypto {

ByteArray rc4(const ByteArray& dataIn, const ByteArray& key) {
    ByteArray data = dataIn;
    rc4Inplace(data, key);
    return data;
}


void rc4Inplace(ByteArray& data, const ByteArray& key) {
    unsigned char s[256];
    rc4Init(s, key);
    int i = 0, j = 0, t = 0;
    unsigned long k = 0;
    unsigned char tmp;
    for (k = 0; k < data.size(); k++) {
        i = (i + 1) % 256;
        j = (j + s[i]) % 256;
        tmp = s[i];
        s[i] = s[j];
        s[j] = tmp;
        t = (s[i] + s[j]) % 256;
        data[k] = data[k] ^ s[t];
    }

}


bool rc4Verify(const ByteArray& key, const ByteArray& challenge, const ByteArray& cipher) {
    ByteArray data = rc4(challenge, key);
    return data == cipher;
}



int64_t rc4Verify(const ArrayList<ByteArray>& keyring, const ByteArray& challenge, const ByteArray& cipher) {
    for (size_t i = 0; i < keyring.size(); i++) {
        if (rc4Verify(keyring[i], challenge, cipher))
            return int64_t(i);
    }

    return -1;
}


}
