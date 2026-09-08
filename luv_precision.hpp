#pragma once

#include <cstdint>
#include <string_view>
#include <cmath>

namespace luv {

enum class AssetClass : uint8_t {
    kEquity = 1,
    kForeignExchange = 2,
    kCryptocurrency = 3,
    kFixedIncome = 4,
    kCommodity = 5
};

// Fixed-point scaling descriptor for multi-asset trading
struct InstrumentMetadata {
    uint16_t symbol_idx = 0;
    AssetClass asset_class = AssetClass::kEquity;
    char ticker[16] = {0};
    uint32_t price_decimals = 2;    // Equities: 2 (0.01), FX: 5 (0.00001), Crypto: 2/4
    uint32_t qty_decimals = 0;      // Equities: 0 (1 share), Crypto: 8 (0.00000001 satoshis)
    int64_t min_order_qty = 1;      // In scaled units
    int64_t max_order_qty = 100'000'000;
    int64_t tick_size = 1;          // In scaled price units
    int64_t min_notional = 100;     // Min notional in base currency cents/units
    int64_t contract_multiplier = 1;
};

class PrecisionConverter {
public:
    // Scale decimal float/double to internal fixed-point integer
    static constexpr int64_t to_scaled_price(double price, uint32_t decimals) noexcept {
        int64_t factor = 1;
        for (uint32_t i = 0; i < decimals; ++i) factor *= 10;
        return static_cast<int64_t>(std::round(price * factor));
    }

    // Convert internal fixed-point integer to double
    static constexpr double from_scaled_price(int64_t scaled, uint32_t decimals) noexcept {
        int64_t factor = 1;
        for (uint32_t i = 0; i < decimals; ++i) factor *= 10;
        return static_cast<double>(scaled) / static_cast<double>(factor);
    }

    // Scale quantity with fractional decimal support
    static constexpr int64_t to_scaled_qty(double qty, uint32_t decimals) noexcept {
        int64_t factor = 1;
        for (uint32_t i = 0; i < decimals; ++i) factor *= 10;
        return static_cast<int64_t>(std::round(qty * factor));
    }

    static constexpr double from_scaled_qty(int64_t scaled, uint32_t decimals) noexcept {
        int64_t factor = 1;
        for (uint32_t i = 0; i < decimals; ++i) factor *= 10;
        return static_cast<double>(scaled) / static_cast<double>(factor);
    }

    // Validates order against instrument tick size and lot bounds
    static bool validate_order_bounds(
        const InstrumentMetadata& meta,
        int64_t price,
        int64_t qty,
        const char** out_err_reason = nullptr) noexcept
    {
        if (qty < meta.min_order_qty) {
            if (out_err_reason) *out_err_reason = "Order quantity below minimum lot size";
            return false;
        }
        if (qty > meta.max_order_qty) {
            if (out_err_reason) *out_err_reason = "Order quantity exceeds maximum allowed";
            return false;
        }
        if (meta.tick_size > 1 && (price % meta.tick_size) != 0) {
            if (out_err_reason) *out_err_reason = "Price violates minimum tick size increment";
            return false;
        }
        // Calculate notional value scaled
        // Notional = (Price * Qty) / (10^price_decimals * 10^qty_decimals)
        int64_t price_factor = 1;
        for (uint32_t i = 0; i < meta.price_decimals; ++i) price_factor *= 10;
        int64_t qty_factor = 1;
        for (uint32_t i = 0; i < meta.qty_decimals; ++i) qty_factor *= 10;

        int64_t notional = (price * qty * meta.contract_multiplier) / (price_factor * qty_factor);
        if (meta.min_notional > 0 && notional < meta.min_notional) {
            if (out_err_reason) *out_err_reason = "Order notional below minimum allowed";
            return false;
        }

        return true;
    }
};

} // namespace luv
