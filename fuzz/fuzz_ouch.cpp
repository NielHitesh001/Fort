#include "luv_ouch.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    luv::ouch::Parser parser;
    luv::ouch::Event event{};
    size_t consumed = 0;
    if (parser.parse(data, size, event, consumed) && consumed > size) std::abort();
    return 0;
}
