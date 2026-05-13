/*
    TCP/IPv4 Socket

    created on 2025.2.6 at Xiangzhou, Zhuhai, Guangdong

    Modified 2025 – connect() now goes through HyperAmpBridge instead of
    POSIX sockets.  start() and accept() are kept as-is (server-side logic
    still uses the seL4 POSIX shim for the local listening socket).

    gongty [at] tongji [dot] edu [dot] cn
*/

#include <monkey/net/Socket4.h>
#include <monkey/net/HyperAmpBridge.h>

#include <sys/socket.h>

#include <base/log.h>
#include <adl/TString.h>


using namespace monkey;


namespace monkey::net {


Status Socket4::connect() {
    close();

    Genode::log("[Socket4] Connecting via HyperAmpBridge to ",
                ip.toString().c_str(), " : ", port);

    auto& bridge = HyperAmpBridge::instance();

    /* Ensure the bridge is initialised (idempotent). */
    bridge.init();

    if (!bridge.connect(ip, port)) {
        Genode::error("[Socket4] HyperAmpBridge connect failed");
        return Status::NETWORK_ERROR;
    }

    /*
     * Set socketFd to a sentinel value > 1 so that valid() returns true.
     * The actual I/O bypasses this fd entirely (routed through the bridge
     * in PromisedSocketIo::recv / send / close), but upper layers check
     * valid() before proceeding.
     */
    socketFd = 9999;

    return Status::SUCCESS;
}


Status Socket4::start(adl::size_t maxClients) {
    if (maxClients > INT32_MAX) {
        return Status::INVALID_PARAMETERS;
    }
    
    close();
    
    socketFd = socket(AF_INET, SOCK_STREAM, 0);

    if (socketFd == 0) {
        return Status::NETWORK_ERROR;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip.ui32;
    addr.sin_port = adl::htons(port);

    if (bind(socketFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close();
        return Status::NETWORK_ERROR;
    }

    if (listen(socketFd, adl::int32_t(maxClients)) < 0) {
        close();
        return Status::NETWORK_ERROR;
    }

    Genode::log("Server started. Addr: ", ip.toString().c_str(), " : ", port);
    return Status::SUCCESS;
}


Socket4 Socket4::accept(bool ignoreError) {
    while (true) {
        Genode::log("Waiting for connection...");

        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        auto fd = ::accept(socketFd, (struct sockaddr*)&addr, &addrlen);

        if (fd != -1) {
            Socket4 s;
            s.socketFd = fd;
            s.ip.ui32 = addr.sin_addr.s_addr;
            s.port = adl::ntohs(addr.sin_port);
            return s;
        }
        else if (!ignoreError) {
            return Socket4 {};
        }
    }
}


}  // namespace monkey::net
