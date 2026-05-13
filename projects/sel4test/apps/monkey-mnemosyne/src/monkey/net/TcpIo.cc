/*
 * Monkey Net :: TcpIo
 *
 * Created on 2025.1.21 at Xiangzhou, Zhuhai, Guangdong
 *
 * Modified 2025 – route all I/O through HyperAmpBridge instead of POSIX
 * sockets so that monkey-mnemosyne can operate on seL4 (no network stack).
 *
 * gongty  [at] tongji [dot] edu [dot] cn
 */


#include <base/log.h>
#include <monkey/net/TcpIo.h>
#include <monkey/net/HyperAmpBridge.h>

namespace monkey::net {


void PromisedSocketIo::close() {
    HyperAmpBridge::instance().close();
    socketFd = -1;
}


adl::int64_t PromisedSocketIo::recv(void* buf, adl::size_t len) {
    return HyperAmpBridge::instance().recv(buf, len);
}


adl::int64_t PromisedSocketIo::send(const void* buf, adl::size_t len) {
    return HyperAmpBridge::instance().send(buf, len);
}


}
