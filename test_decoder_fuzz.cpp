#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>

#include "luv_decode_itch.hpp"

int main() {
    luv::SymbolTable symbols;
    const std::array<char, 8> ticker = {'S', '0', '0', '1', ' ', ' ', ' ', ' '};
    assert(symbols.insert(ticker.data(), 1));

    std::mt19937_64 rng(0xC0FFEE);
    luv::TickMsg output{};

    const std::array<std::pair<uint8_t, size_t>, 8> message_lengths = {{
        {'A', luv::itch::kLenAddOrder},
        {'F', luv::itch::kLenAddOrderMPID},
        {'E', luv::itch::kLenOrderExecuted},
        {'C', luv::itch::kLenOrderExecPrice},
        {'X', luv::itch::kLenOrderCancel},
        {'D', luv::itch::kLenOrderDelete},
        {'U', luv::itch::kLenOrderReplace},
        {'P', luv::itch::kLenTrade},
    }};
    for (const auto& [message_type, valid_length] : message_lengths) {
        std::array<uint8_t, luv::itch::kLenTrade> input{};
        input[0] = message_type;
        for (size_t length = 0; length < valid_length; ++length) {
            assert(!luv::decode_itch(input.data(), length, symbols, output));
        }
    }

    uint32_t accepted = 0;
    for (uint32_t iteration = 0; iteration < 100'000; ++iteration) {
        std::array<uint8_t, 64> input{};
        for (uint8_t& byte : input) byte = static_cast<uint8_t>(rng());
        const size_t length = static_cast<size_t>(rng() % input.size());
        if (luv::decode_itch(input.data(), length, symbols, output)) {
            ++accepted;
            assert(output.symbol_idx < luv::Config::kSymbols);
            assert(output.order_ref != 0);
                 assert(output.qty > 0 || output.msg_type == 'D');
                 assert(output.price > 0 || output.msg_type == 'E' ||
                     output.msg_type == 'X' || output.msg_type == 'D');
        }
    }

    assert(accepted < 100);
    assert(!luv::decode_itch(nullptr, 0, symbols, output));
    std::printf("decoder fuzz smoke passed: 100000 malformed inputs rejected\n");
    return 0;
}
