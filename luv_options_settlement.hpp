#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace derivatives {

enum class OptionType : uint8_t {
    kCall = 0,
    kPut = 1
};

enum class SettlementStyle : uint8_t {
    kCashSettled = 0,
    kPhysicalDelivery = 1
};

enum class SettlementAction : uint8_t {
    kLapsedOTM = 0,
    kExercisedCash = 1,
    kExercisedPhysicalDelivery = 2
};

struct OptionContractPosition {
    uint64_t contract_id = 0;
    uint16_t underlying_symbol_idx = 0;
    OptionType option_type = OptionType::kCall;
    SettlementStyle settlement_style = SettlementStyle::kCashSettled;
    int64_t strike_price = 0; // Scaled price (x10,000)
    int64_t contract_size = 100; // Standard 100 shares per contract
    int64_t position_qty = 0;    // Positive = Long, Negative = Short
};

struct OptionSettlementResult {
    uint64_t contract_id = 0;
    SettlementAction action = SettlementAction::kLapsedOTM;
    int64_t cash_settlement_pnl = 0;      // Total net cash for holder
    int64_t physical_shares_deliver = 0; // Shares to buy/deliver
    int64_t physical_cash_flow = 0;      // Delivery cost
};

class OptionsSettlementEngine {
public:
    static constexpr size_t kMaxPositions = 256;

    OptionsSettlementEngine() noexcept : num_positions_(0) {}

    bool register_position(const OptionContractPosition& pos) noexcept {
        if (num_positions_ >= kMaxPositions) return false;
        positions_[num_positions_++] = pos;
        return true;
    }

    // Settles all registered option positions at expiry against final underlying settlement price
    size_t settle_all(int64_t underlying_settlement_price,
                      OptionSettlementResult* out_results,
                      size_t max_results) noexcept {
        if (!out_results || max_results == 0) return 0;

        size_t count = 0;
        for (size_t i = 0; i < num_positions_ && count < max_results; ++i) {
            const auto& pos = positions_[i];
            if (pos.position_qty == 0) continue;

            bool is_itm = false;
            int64_t itm_diff = 0;

            if (pos.option_type == OptionType::kCall) {
                if (underlying_settlement_price > pos.strike_price) {
                    is_itm = true;
                    itm_diff = underlying_settlement_price - pos.strike_price;
                }
            } else { // Put
                if (underlying_settlement_price < pos.strike_price) {
                    is_itm = true;
                    itm_diff = pos.strike_price - underlying_settlement_price;
                }
            }

            OptionSettlementResult res{};
            res.contract_id = pos.contract_id;

            if (!is_itm) {
                res.action = SettlementAction::kLapsedOTM;
                res.cash_settlement_pnl = 0;
                res.physical_shares_deliver = 0;
                res.physical_cash_flow = 0;
            } else {
                if (pos.settlement_style == SettlementStyle::kCashSettled) {
                    res.action = SettlementAction::kExercisedCash;
                    // Cash PnL = itm_diff * contract_size * position_qty / 10000 (if scaling)
                    res.cash_settlement_pnl = (itm_diff * pos.contract_size * pos.position_qty) / 10000;
                } else {
                    res.action = SettlementAction::kExercisedPhysicalDelivery;
                    int64_t total_shares = pos.position_qty * pos.contract_size;
                    if (pos.option_type == OptionType::kCall) {
                        res.physical_shares_deliver = total_shares; // Long call receives shares
                        res.physical_cash_flow = -((total_shares * pos.strike_price) / 10000); // pays strike
                    } else {
                        res.physical_shares_deliver = -total_shares; // Long put delivers shares
                        res.physical_cash_flow = (total_shares * pos.strike_price) / 10000; // receives strike
                    }
                }
            }

            out_results[count++] = res;
        }

        return count;
    }

private:
    std::array<OptionContractPosition, kMaxPositions> positions_{};
    size_t num_positions_{0};
};

} // namespace derivatives
} // namespace luv
