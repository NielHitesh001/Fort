#include "luv_decode_itch.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    luv::SymbolTable symbols;
    (void)symbols.insert("S001    ", 1);
    luv::TickMsg output{};
    if (luv::decode_itch(data, size, symbols, output) &&
        (output.symbol_idx >= luv::Config::kSymbols || output.order_ref == 0)) {
        std::abort();
    }
    return 0;
}
