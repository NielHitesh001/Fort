#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>
#include <string_view>

namespace luv {

enum class FXOrderSide : uint8_t {
    Buy = 0,
    Sell = 1
};

struct FXQuote {
    char pair[8]{0};           // e.g. "EURUSD", "USDJPY", "EURJPY"
    double bid{0.0};
    double ask{0.0};
    uint64_t bid_size{0};
    uint64_t ask_size{0};
    double fee_rate{0.0001};   // 1 bp exchange fee
    double est_slippage{0.00005}; // 0.5 bp estimated slippage
};

struct TriangularArbLeg {
    char pair[8]{0};
    FXOrderSide side{FXOrderSide::Buy};
    double execution_price{0.0};
    double conversion_rate{0.0};
    uint64_t max_executable_size{0};
};

struct TriangularOpportunity {
    bool opportunity_found{false};
    char base_ccy[4]{0};       // e.g. "USD"
    char intermediate_1[4]{0}; // e.g. "EUR"
    char intermediate_2[4]{0}; // e.g. "JPY"
    double gross_multiplier{1.0};
    double net_profit_bp{0.0}; // in basis points
    double net_profit_ratio{0.0};
    double max_base_notional{0.0};
    std::array<TriangularArbLeg, 3> legs{};
};

struct FXTriangularConfig {
    double min_profit_threshold_bp{2.0}; // Minimum 2 bp profit to trigger
    double max_position_limit_usd{5'000'000.0};
};

class FXTriangularArbEngine {
public:
    explicit FXTriangularArbEngine(const FXTriangularConfig& cfg = FXTriangularConfig{}) noexcept
        : config_(cfg) {}

    // Evaluate standard USD -> EUR -> JPY -> USD triangle
    // Quotes needed: EUR/USD, EUR/JPY, USD/JPY
    TriangularOpportunity evaluate_eur_jpy_usd_triangle(
        const FXQuote& eurusd,
        const FXQuote& eurjpy,
        const FXQuote& usdjpy) const noexcept {

        TriangularOpportunity opp{};
        std::strncpy(opp.base_ccy, "USD", 3);
        std::strncpy(opp.intermediate_1, "EUR", 3);
        std::strncpy(opp.intermediate_2, "JPY", 3);

        // Path 1: USD -> EUR -> JPY -> USD
        // 1. Sell USD, buy EUR: Buy EURUSD at ask price -> Rate: 1 / eurusd.ask
        double rate1_fwd = (eurusd.ask > 0.0) ? (1.0 / eurusd.ask) : 0.0;
        double cost1_fwd = (1.0 - eurusd.fee_rate - eurusd.est_slippage);

        // 2. Sell EUR, buy JPY: Sell EURJPY at bid price -> Rate: eurjpy.bid
        double rate2_fwd = eurjpy.bid;
        double cost2_fwd = (1.0 - eurjpy.fee_rate - eurjpy.est_slippage);

        // 3. Sell JPY, buy USD: Buy USDJPY at ask price -> Rate: 1 / usdjpy.ask
        double rate3_fwd = (usdjpy.ask > 0.0) ? (1.0 / usdjpy.ask) : 0.0;
        double cost3_fwd = (1.0 - usdjpy.fee_rate - usdjpy.est_slippage);

        double gross_fwd = rate1_fwd * rate2_fwd * rate3_fwd;
        double net_fwd = (rate1_fwd * cost1_fwd) * (rate2_fwd * cost2_fwd) * (rate3_fwd * cost3_fwd);

        // Path 2: USD -> JPY -> EUR -> USD (Reverse triangle)
        // 1. Sell USD, buy JPY: Buy USDJPY at ask -> Rate: usdjpy.bid (Sell USD, get JPY at bid)
        double rate1_rev = usdjpy.bid;
        double cost1_rev = (1.0 - usdjpy.fee_rate - usdjpy.est_slippage);

        // 2. Sell JPY, buy EUR: Buy EURJPY at ask -> Rate: 1 / eurjpy.ask
        double rate2_rev = (eurjpy.ask > 0.0) ? (1.0 / eurjpy.ask) : 0.0;
        double cost2_rev = (1.0 - eurjpy.fee_rate - eurjpy.est_slippage);

        // 3. Sell EUR, buy USD: Sell EURUSD at bid -> Rate: eurusd.bid
        double rate3_rev = eurusd.bid;
        double cost3_rev = (1.0 - eurusd.fee_rate - eurusd.est_slippage);

        double gross_rev = rate1_rev * rate2_rev * rate3_rev;
        double net_rev = (rate1_rev * cost1_rev) * (rate2_rev * cost2_rev) * (rate3_rev * cost3_rev);

        if (net_fwd > net_rev && net_fwd > 1.0) {
            double profit_bp = (net_fwd - 1.0) * 10'000.0;
            if (profit_bp >= config_.min_profit_threshold_bp) {
                opp.opportunity_found = true;
                opp.gross_multiplier = gross_fwd;
                opp.net_profit_ratio = net_fwd - 1.0;
                opp.net_profit_bp = profit_bp;

                // Configure Leg 1: Buy EURUSD
                std::strncpy(opp.legs[0].pair, "EURUSD", 6);
                opp.legs[0].side = FXOrderSide::Buy;
                opp.legs[0].execution_price = eurusd.ask;
                opp.legs[0].conversion_rate = rate1_fwd;
                opp.legs[0].max_executable_size = eurusd.ask_size;

                // Leg 2: Sell EURJPY
                std::strncpy(opp.legs[1].pair, "EURJPY", 6);
                opp.legs[1].side = FXOrderSide::Sell;
                opp.legs[1].execution_price = eurjpy.bid;
                opp.legs[1].conversion_rate = rate2_fwd;
                opp.legs[1].max_executable_size = eurjpy.bid_size;

                // Leg 3: Sell JPY to Buy USD
                std::strncpy(opp.legs[2].pair, "USDJPY", 6);
                opp.legs[2].side = FXOrderSide::Sell;
                opp.legs[2].execution_price = usdjpy.ask;
                opp.legs[2].conversion_rate = rate3_fwd;
                opp.legs[2].max_executable_size = usdjpy.ask_size;

                opp.max_base_notional = std::min({
                    config_.max_position_limit_usd,
                    static_cast<double>(eurusd.ask_size) * eurusd.ask,
                    static_cast<double>(eurjpy.bid_size) * eurusd.ask,
                    static_cast<double>(usdjpy.ask_size)
                });
                return opp;
            }
        } else if (net_rev > 1.0) {
            double profit_bp = (net_rev - 1.0) * 10'000.0;
            if (profit_bp >= config_.min_profit_threshold_bp) {
                opp.opportunity_found = true;
                opp.gross_multiplier = gross_rev;
                opp.net_profit_ratio = net_rev - 1.0;
                opp.net_profit_bp = profit_bp;

                // Reverse Legs
                std::strncpy(opp.legs[0].pair, "USDJPY", 6);
                opp.legs[0].side = FXOrderSide::Buy;
                opp.legs[0].execution_price = usdjpy.bid;
                opp.legs[0].conversion_rate = rate1_rev;
                opp.legs[0].max_executable_size = usdjpy.bid_size;

                std::strncpy(opp.legs[1].pair, "EURJPY", 6);
                opp.legs[1].side = FXOrderSide::Buy;
                opp.legs[1].execution_price = eurjpy.ask;
                opp.legs[1].conversion_rate = rate2_rev;
                opp.legs[1].max_executable_size = eurjpy.ask_size;

                std::strncpy(opp.legs[2].pair, "EURUSD", 6);
                opp.legs[2].side = FXOrderSide::Sell;
                opp.legs[2].execution_price = eurusd.bid;
                opp.legs[2].conversion_rate = rate3_rev;
                opp.legs[2].max_executable_size = eurusd.bid_size;

                opp.max_base_notional = std::min({
                    config_.max_position_limit_usd,
                    static_cast<double>(usdjpy.bid_size),
                    static_cast<double>(eurjpy.ask_size) * eurusd.bid,
                    static_cast<double>(eurusd.bid_size) * eurusd.bid
                });
                return opp;
            }
        }

        return opp;
    }

private:
    FXTriangularConfig config_{};
};

} // namespace luv
