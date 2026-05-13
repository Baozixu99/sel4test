// ddst.sjtu.edu.cn

#include <adl/sys/types.h>
#include <adl/string.h>

#include <mnemosyne_api.h>
#include <monkey/log.h>

#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>

#include "config.h"

static bool isApp1() {
    return labConfig.labApp == LabApp::App1;
}

static bool isApp2() {
    return labConfig.labApp == LabApp::App2;
}

static mnemosyne_session_t* createMnemosyneSession() {
    const char* appKey = isApp1() ? labConfig.app1Key : labConfig.app2Key;
    adl::uint8_t role = isApp1() ? MNEMOSYNE_ROLE_ALLOC : MNEMOSYNE_ROLE_REF;

    if (mnemosyne_engine_init(
            labConfig.mnemosyneIp,
            labConfig.mnemosynePort,
            appKey,
            MNEMOSYNE_CHANNEL_DEFAULT,
            role
        ) != 0) {
        Genode::error("Failed to init mnemosyne engine.");
        return nullptr;
    }

    mnemosyne_session_t* sess = mnemosyne_sess_new();
    if (!sess) {
        Genode::error("Failed to create mnemosyne session.");
        return nullptr;
    }

    while (true) {
        int rc = mnemosyne_sess_connect_by_addrstr_devid(
            sess,
            MNEMOSYNE_PROTO_TCP,
            nullptr,
            MNEMOSYNE_DEV_AUTO
        );
        if (rc == 0) {
            return sess;
        }

        if (isApp2() && rc == -8) {
            usleep(1907 * 1000);
            continue;
        }

        Genode::error("Failed to connect/auth with server. rc=", rc);
        mnemosyne_sess_close(sess);
        return nullptr;
    }
}


struct MonkeySharedPage {
    adl::uint64_t magics[4];

    adl::int64_t strVer;

    char str[1024];

    adl::int64_t zero0;

    adl::int8_t paddings[3024];

} __attribute__((__packed__));

static const adl::uint64_t monkeyPageMagics[4] = {
    0x544b65123da24f5aLL, 0x8ec8154e934c885dLL,
    0xdc8b8114553c472cLL, 0xa463d81acd9e4e22LL
};


static char localPage[4096] __attribute__((__aligned__(4096)));

static adl::int64_t currentStrVer = 1;

static_assert(sizeof(MonkeySharedPage) == 4096, "MonkeySharedPage must be 4KB.");


static int waitDataUpdate(mnemosyne_session_t* sess) {
    while (true) {
        usleep(1000 * 1000);

        int recvRc = mnemosyne_sess_recv(sess, localPage, sizeof(localPage));
        if (recvRc < 0) {
            Genode::error("Failed to read page from server.");
            return recvRc;
        }

        if (currentStrVer != ((MonkeySharedPage*)localPage)->strVer) {
            currentStrVer = ((MonkeySharedPage*)localPage)->strVer;
            break;
        }
    }

    return 0;
}


static int doLabApp1(mnemosyne_session_t* sess) {
    MonkeySharedPage& sharedPage = *(MonkeySharedPage*)localPage;

    adl::memcpy(sharedPage.magics, monkeyPageMagics, sizeof(sharedPage.magics));
    adl::strcpy(sharedPage.str, "Hello from DDST, Shanghai Jiao Tong University! This is Lab App 1 speaking.");
    sharedPage.strVer = currentStrVer;
    sharedPage.zero0 = 0;

    // upload page to mnemosyne.
    int sendRc = mnemosyne_sess_send(sess, localPage, sizeof(localPage));
    if (sendRc < 0) {
        Genode::error("Failed to write page to server.");
        return sendRc;
    }

    // wait for data change.
    if (waitDataUpdate(sess) < 0) {
        Genode::error("Failed to wait for data update.");
        return -1;
    }

    Genode::log("Data updated, new strVer: ", currentStrVer);
    Genode::log("New str: ", ((MonkeySharedPage*)localPage)->str);

    // write response.

    adl::strcpy(sharedPage.str, "Got your message, Lab App 1 here. Nice to meet you!");
    sharedPage.strVer = ++currentStrVer;
    sendRc = mnemosyne_sess_send(sess, localPage, sizeof(localPage));
    if (sendRc < 0) {
        Genode::error("Failed to write page to server.");
        return sendRc;
    }


    return 0;
}


static int doLabApp2(mnemosyne_session_t* sess) {
    MonkeySharedPage& sharedPage = *(MonkeySharedPage*)localPage;

    // wait for magic match.
    while (true) {
        usleep(1000 * 1000);

        int recvRc = mnemosyne_sess_recv(sess, localPage, sizeof(localPage));
        if (recvRc < 0) {
            Genode::error("Failed to read page from server.");
            return recvRc;
        }

        if (adl::memcmp(sharedPage.magics, monkeyPageMagics, sizeof(sharedPage.magics)) == 0) {
            break;
        }
    }

    // read the string.
    currentStrVer = sharedPage.strVer;
    Genode::log("Got strVer ", currentStrVer, " and str: ", sharedPage.str);

    // write response.
    adl::strcpy(sharedPage.str, "Got your message, Lab App 2 here. Nice to meet you!");
    sharedPage.strVer = ++currentStrVer;

    int sendRc = mnemosyne_sess_send(sess, localPage, sizeof(localPage));
    if (sendRc < 0) {
        Genode::error("Failed to write page to server.");
        return sendRc;
    }

    // read response from server.
    if (waitDataUpdate(sess) < 0) {
        Genode::error("Failed to wait for data update.");
        return -1;
    }

    Genode::log("Data updated, new strVer: ", currentStrVer);
    Genode::log("New str: ", ((MonkeySharedPage*)localPage)->str);

    return 0;
}


int main() {
    mnemosyne_session_t* sess = createMnemosyneSession();
    if (!sess) {
        Genode::error("Failed to create session with server.");
        return -1;
    }
    
    int rc = 0;
    if (isApp1()) {
        rc = doLabApp1(sess);
    }
    else if (isApp2()) {
        rc = doLabApp2(sess);
    }
    else {
        Genode::error("Unknown lab app.");
        rc = -1;
    }

    mnemosyne_sess_close(sess);
    return rc;
}
