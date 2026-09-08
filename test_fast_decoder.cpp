#include <iostream>
#include <cassert>
#include <cstring>
#include "luv_fast_decoder.hpp"

int main() {
    std::cout << "[TEST] Running FIX FAST Protocol Decoder Test...\n";

    // 1. Test Stop-bit unsigned integer decoding
    // Number 942 = 7 * 128 + 46
    // 7-bit chunks: [0x07], [0x2E | 0x80 = 0xAE]
    // Bytes: 0x07, 0xAE
    const uint8_t u_bytes[] = { 0x07, 0xAE };
    const uint8_t* ptr_u = u_bytes;
    size_t len_u = sizeof(u_bytes);
    uint64_t val_u = 0;
    assert(luv::FastDecoder::decode_uint(ptr_u, len_u, val_u));
    assert(val_u == 942);
    assert(len_u == 0);

    // 2. Test Presence Map (PMAP)
    // PMAP byte: 0xC0 (1100 0000 -> bit 7=1 (MSB stop bit), bits 6..0: 1000000 -> 1 followed by 0s)
    const uint8_t pmap_bytes[] = { 0xC0 };
    const uint8_t* ptr_p = pmap_bytes;
    size_t len_p = sizeof(pmap_bytes);
    luv::FastPmap pmap;
    assert(luv::FastDecoder::decode_pmap(ptr_p, len_p, pmap));
    assert(pmap.next_bit() == true);  // Bit 1 = 1
    assert(pmap.next_bit() == false); // Bit 2 = 0

    // 3. Test FAST ASCII string decoding: "AAPL"
    // 'A' = 0x41, 'A' = 0x41, 'P' = 0x50, 'L' = 0x4C | 0x80 = 0xCC
    const uint8_t str_bytes[] = { 'A', 'A', 'P', static_cast<uint8_t>('L' | 0x80) };
    const uint8_t* ptr_s = str_bytes;
    size_t len_s = sizeof(str_bytes);
    char out_str[16];
    assert(luv::FastDecoder::decode_ascii(ptr_s, len_s, out_str, sizeof(out_str)));
    assert(std::strcmp(out_str, "AAPL") == 0);

    std::cout << "[TEST] Decoded UInt: " << val_u << " | Decoded String: " << out_str << "\n";
    std::cout << "[TEST] FIX FAST Protocol Decoder Test Passed!\n";
    return 0;
}
