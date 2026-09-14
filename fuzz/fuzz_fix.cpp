#include "luv_fix.hpp"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    luv::fix::FixMessageView message;
    if (message.parse(reinterpret_cast<const char*>(data), size)) {
        volatile int64_t quantity = message.get_int(luv::fix::Tag::OrderQty);
        volatile int64_t sequence = message.get_int(luv::fix::Tag::MsgSeqNum);
        (void)quantity;
        (void)sequence;
    }
    return 0;
}
