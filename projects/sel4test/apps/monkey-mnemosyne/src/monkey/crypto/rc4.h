/*

    RC4


    Created on 2025.1.6 at Jiangchuan, Minhang, Shanghai

    gongty  [at] tongji [dot] edu [dot] cn
    feng.yt [at]  sjtu  [dot] edu [dot] cn

    
*/


#pragma once

#include <adl/collections/ArrayList.hpp>
#include <adl/sys/types.h>



namespace monkey::crypto {


adl::ByteArray rc4(const adl::ByteArray& dataIn, const adl::ByteArray& key);


void rc4Inplace(adl::ByteArray& data, const adl::ByteArray& key);


bool rc4Verify(const adl::ByteArray& key, const adl::ByteArray& challenge, const adl::ByteArray& cipher);


/**
 * 
 * @return -1 if not found. 
 */
adl::int64_t rc4Verify(
    const adl::ArrayList<adl::ByteArray>& keyring, 
    const adl::ByteArray& challenge, 
    const adl::ByteArray& cipher
);



}

