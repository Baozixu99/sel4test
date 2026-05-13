/*
 * Basic type defines
 *
 * Created on 2025.1.21 at Xiangzhou, Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */

#include <monkey/net/protocol/defines.h>

namespace monkey::net::protocol {

const char* MAGIC = "mkOS";


const char* msgTypeToString(MsgType type) {
    switch (type) {
        case MsgType::None:
            return "None";


        case MsgType::Response:
            return "Response";

    
        case MsgType::Hello:
            return "Hello";

        case MsgType::Auth:
            return "Auth";

    
        case MsgType::GetIdentityKeys:
            return "GetIdentityKeys";

    
        case MsgType::MemoryNodeShowId:
            return "MemoryNodeShowId";

        case MsgType::MemoryNodeClockIn:
            return "MemoryNodeClockIn";

        case MsgType::MemoryNodeClockOut:
            return "MemoryNodeClockOut";

        case MsgType::MemoryNodeHandover:
            return "MemoryNodeHandover";

        case MsgType::LocateMemoryNodes:
            return "LocateMemoryNodes";

    
        case MsgType::TryAlloc:
            return "TryAlloc";

        case MsgType::ReadBlock:
            return "ReadBlock";

        case MsgType::WriteBlock:
            return "WriteBlock";

        case MsgType::CheckAvailMem:
            return "CheckAvailMem";

        case MsgType::FreeBlock:
            return "FreeBlock";


        case MsgType::RefBlock:
            return "RefBlock";

        case MsgType::UnrefBlock:
            return "UnrefBlock";

        case MsgType::GetBlockDataVersion:
            return "GetBlockDataVersion";

    
        case MsgType::PingPong:
            return "PingPong";


        default:
            return "UNDEFINED";
    };
}


}
