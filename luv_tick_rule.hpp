#pragma once

#include <cstdint>
#include <array>
#include <algorithm>

namespace luv {
namespace rules {

// SEC Rule 612 Sub-Penny Rule:
// - Orders >= $1.00 (10000 in fixed-point cents/scaled) must be in multiples of $0.01 (100)
// - Orders < $1.00 must be in multiples of $0.0001 (1)
class SecRule612Validator {
public:
    static constexpr int64_t kOneDollarScaled = 10000; // $1.00 in 4-decimal fixed point
    static constexpr int64_t kPennyTick = 100;         // $0.01 in 4-decimal fixed point
    static constexpr int64_t kSubPennyTick = 1;        // $0.0001 in 4-decimal fixed point

    static bool validate_tick(int64_t price, const char** out_reason = nullptr) noexcept {
        if (price <= 0) {
            if (out_reason) *out_reason = "Price must be strictly positive";
            return false;
        }

        if (price >= kOneDollarScaled) {
            if ((price % kPennyTick) != 0) {
                if (out_reason) *out_reason = "SEC Rule 612 Violation: Orders >= $1.00 must have minimum tick of $0.01";
                return false;
            }
        } else {
            if ((price % kSubPennyTick) != 0) {
                if (out_reason) *out_reason = "SEC Rule 612 Violation: Orders < $1.00 must have minimum tick of $0.0001";
                return false;
            }
        }

        return true;
    }
};

// MiFID II RTS 28 Dynamic Liquidity Band Tick Size Tables
struct MifidTickBand {
    int64_t price_lower;
    int64_t price_upper;
    int64_t min_tick;
};

class MifidDynamicTickValidator {
public:
    static constexpr size_t kMaxBands = 8;

    // Standard Liquidity Band 6 (High Liquidity ADNT >= 9000):
    // 0.00 - 0.50: 0.0001
    // 0.50 - 1.00: 0.0002
    // 1.00 - 2.00: 0.0005
    // 2.00 - 5.00: 0.0010
    // 5.00 - 10.00: 0.0020
    // 10.00 - 20.00: 0.0050
    // 20.00 - 50.00: 0.0100
    // 50.00+: 0.0200
    static constexpr std::array<MifidTickBand, 8> kBand6Tables = {{
        {0, 5000, 1},
        {5000, 10000, 2},
        {10000, 20000, 5},
        {20000, 50000, 10},
        {50000, 100000, 20},
        {100000, 200000, 50},
        {200000, 500000, 100},
        {500000, INT64_MAX, 200}
    }};

    static int64_t get_min_tick(int64_t price) noexcept {
        for (const auto& band : kBand6Tables) {
            if (price >= band.price_lower && price < band.price_upper) {
                return band.min_tick;
            }
        }
        return 1;
    }

    static bool validate_mifid_tick(int64_t price, const char** out_reason = nullptr) noexcept {
        if (price <= 0) return false;
        int64_t required_tick = get_min_tick(price);
        if ((price % required_tick) != 0) {
            if (out_reason) *out_reason = "MiFID II Tick Size Regime Violation";
            return false;
        }
        return true;
    }
};

} // namespace rules
} // namespace luv
