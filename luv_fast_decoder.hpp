#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>

namespace luv {

struct FastPmap {
    uint32_t bits{0};
    uint8_t bit_count{0};
    uint8_t current_bit{0};

    bool next_bit() noexcept {
        if (current_bit >= bit_count) return false;
        bool val = (bits & (1U << (bit_count - 1 - current_bit))) != 0;
        ++current_bit;
        return val;
    }
};

class FastDecoder {
public:
    // Decodes Stop-bit encoded unsigned integer (7 bits per byte, MSB set on last byte)
    static bool decode_uint(const uint8_t*& buf, size_t& len, uint64_t& out_val) noexcept {
        out_val = 0;
        if (len == 0 || !buf) return false;

        while (len > 0) {
            uint8_t b = *buf++;
            --len;
            out_val = (out_val << 7) | (b & 0x7F);
            if (b & 0x80) {
                return true; // Final byte reached
            }
        }
        return false;
    }

    // Decodes Stop-bit encoded signed integer
    static bool decode_int(const uint8_t*& buf, size_t& len, int64_t& out_val) noexcept {
        out_val = 0;
        if (len == 0 || !buf) return false;

        bool sign_bit_set = (*buf & 0x40) != 0;
        if (sign_bit_set) {
            out_val = -1; // Sign extend with 1s
        }

        while (len > 0) {
            uint8_t b = *buf++;
            --len;
            out_val = (out_val << 7) | (b & 0x7F);
            if (b & 0x80) {
                return true;
            }
        }
        return false;
    }

    // Decodes FAST Presence Map (PMAP)
    static bool decode_pmap(const uint8_t*& buf, size_t& len, FastPmap& pmap) noexcept {
        pmap.bits = 0;
        pmap.bit_count = 0;
        pmap.current_bit = 0;

        if (len == 0 || !buf) return false;

        while (len > 0) {
            uint8_t b = *buf++;
            --len;
            pmap.bits = (pmap.bits << 7) | (b & 0x7F);
            pmap.bit_count += 7;
            if (b & 0x80) {
                return true;
            }
        }
        return false;
    }

    // Decodes null-terminated or stop-bit string
    static bool decode_ascii(const uint8_t*& buf, size_t& len, char* out_str, size_t max_out) noexcept {
        if (!out_str || max_out == 0 || len == 0) return false;
        size_t written = 0;

        while (len > 0 && written < max_out - 1) {
            uint8_t b = *buf++;
            --len;
            out_str[written++] = static_cast<char>(b & 0x7F);
            if (b & 0x80) {
                out_str[written] = '\0';
                return true;
            }
        }
        out_str[written] = '\0';
        return false;
    }
};

} // namespace luv
