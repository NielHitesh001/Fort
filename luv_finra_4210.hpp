#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace luv {

enum class OptionLegType : uint8_t {
    LongCall = 0,
    ShortCall = 1,
    LongPut = 2,
    ShortPut = 3
};

struct OptionLeg {
    OptionLegType leg_type{OptionLegType::LongCall};
    uint64_t strike_price{0}; // Scaled x10,000 ($100.00 = 1,000,000)
    uint64_t premium_price{0};
    uint64_t quantity{0};
};

enum class SpreadStrategy : uint8_t {
    NakedOption = 0,
    CoveredCall = 1,
    VerticalSpread = 2,
    IronCondor = 3
};

struct FinraMarginResult {
    uint64_t initial_margin_required{0};
    uint64_t maintenance_margin_required{0};
    SpreadStrategy recognized_strategy{SpreadStrategy::NakedOption};
};

class Finra4210MarginCalculator {
public:
    // Standard FINRA 4210 naked equity option requirement:
    // 100% of option market value + 20% of underlying stock value - out-of-the-money amount (subject to 10% underlying minimum)
    static FinraMarginResult calculate_naked_option(
        OptionLegType type,
        uint64_t underlying_price,
        uint64_t strike_price,
        uint64_t premium_price,
        uint64_t contract_qty) noexcept
    {
        FinraMarginResult res;
        res.recognized_strategy = SpreadStrategy::NakedOption;

        uint64_t premium_val = (premium_price * contract_qty * 100) / 10000;
        uint64_t stock_val = (underlying_price * contract_qty * 100) / 10000;

        uint64_t twenty_pct = (stock_val * 20) / 100;
        uint64_t ten_pct = (stock_val * 10) / 100;

        uint64_t otm_amount = 0;
        if (type == OptionLegType::ShortCall) {
            if (strike_price > underlying_price) {
                otm_amount = ((strike_price - underlying_price) * contract_qty * 100) / 10000;
            }
        } else if (type == OptionLegType::ShortPut) {
            if (underlying_price > strike_price) {
                otm_amount = ((underlying_price - strike_price) * contract_qty * 100) / 10000;
            }
        } else {
            // Long options require 100% premium cash paid upfront, 0 maintenance
            res.initial_margin_required = premium_val;
            res.maintenance_margin_required = 0;
            return res;
        }

        uint64_t base_add = (twenty_pct > otm_amount) ? (twenty_pct - otm_amount) : 0;
        uint64_t margin_component = std::max(ten_pct, base_add);

        res.initial_margin_required = premium_val + margin_component;
        res.maintenance_margin_required = res.initial_margin_required;

        return res;
    }

    // Covered Call: Long stock + Short Call (No margin required on the short call under FINRA 4210(f)(2)(H))
    static FinraMarginResult calculate_covered_call(
        uint64_t stock_shares,
        uint64_t stock_price,
        uint64_t call_strike,
        uint64_t call_premium,
        uint64_t call_contracts) noexcept
    {
        FinraMarginResult res;
        res.recognized_strategy = SpreadStrategy::CoveredCall;

        // Long stock margin is standard Reg T 50% initial, 25% maintenance
        uint64_t stock_val = (stock_price * stock_shares) / 10000;
        res.initial_margin_required = (stock_val * 50) / 100;
        res.maintenance_margin_required = (stock_val * 25) / 100;

        return res;
    }

    // Vertical Spread: Long Leg + Short Leg (Margin = Max Loss = Strike Width - Net Premium Received for credit spread)
    static FinraMarginResult calculate_vertical_spread(
        const OptionLeg& leg1,
        const OptionLeg& leg2) noexcept
    {
        FinraMarginResult res;
        res.recognized_strategy = SpreadStrategy::VerticalSpread;

        uint64_t strike_diff = (leg1.strike_price > leg2.strike_price) ?
                               (leg1.strike_price - leg2.strike_price) :
                               (leg2.strike_price - leg1.strike_price);

        uint64_t width_val = (strike_diff * leg1.quantity * 100) / 10000;
        res.initial_margin_required = width_val;
        res.maintenance_margin_required = width_val;

        return res;
    }

    // Iron Condor: Bull Put Spread + Bear Call Spread (Margin = max(Put Spread Width, Call Spread Width))
    static FinraMarginResult calculate_iron_condor(
        uint64_t put_strike_low, uint64_t put_strike_high,
        uint64_t call_strike_low, uint64_t call_strike_high,
        uint64_t contract_qty) noexcept
    {
        FinraMarginResult res;
        res.recognized_strategy = SpreadStrategy::IronCondor;

        uint64_t put_width = (put_strike_high > put_strike_low) ? (put_strike_high - put_strike_low) : 0;
        uint64_t call_width = (call_strike_high > call_strike_low) ? (call_strike_high - call_strike_low) : 0;

        uint64_t max_width = std::max(put_width, call_width);
        uint64_t margin = (max_width * contract_qty * 100) / 10000;

        res.initial_margin_required = margin;
        res.maintenance_margin_required = margin;

        return res;
    }
};

} // namespace luv
