#pragma once

#include <cstdint>
#include <cmath>
#include <string_view>

namespace luv {
namespace fx {

struct FxQuote {
    int64_t bid_price = 0; // Fixed point x 10^5 (e.g. 1.08500 -> 108500)
    int64_t ask_price = 0;
    uint32_t decimals = 5;
};

struct TriangularArbOpportunity {
    bool detected = false;
    double profit_bps = 0.0;
    bool buy_synthetic = false; // true if Direct > Synthetic (Buy Synthetic, Sell Direct)
    int64_t implied_cross_bid = 0;
    int64_t implied_cross_ask = 0;
};

class FxTriangulationEngine {
public:
    // Computes synthetic Cross Pair (e.g. EUR/JPY) from Base-QuoteA (EUR/USD) and QuoteA-QuoteB (USD/JPY)
    static FxQuote compute_synthetic_cross(
        const FxQuote& pair_a, // EUR/USD (e.g. Bid: 1.0850, Ask: 1.0852)
        const FxQuote& pair_b, // USD/JPY (e.g. Bid: 155.20, Ask: 155.25)
        uint32_t target_decimals = 3) noexcept
    {
        double p_a_bid = static_cast<double>(pair_a.bid_price) / std::pow(10.0, pair_a.decimals);
        double p_a_ask = static_cast<double>(pair_a.ask_price) / std::pow(10.0, pair_a.decimals);

        double p_b_bid = static_cast<double>(pair_b.bid_price) / std::pow(10.0, pair_b.decimals);
        double p_b_ask = static_cast<double>(pair_b.ask_price) / std::pow(10.0, pair_b.decimals);

        double cross_bid = p_a_bid * p_b_bid;
        double cross_ask = p_a_ask * p_b_ask;

        double factor = std::pow(10.0, target_decimals);
        return FxQuote{
            .bid_price = static_cast<int64_t>(std::round(cross_bid * factor)),
            .ask_price = static_cast<int64_t>(std::round(cross_ask * factor)),
            .decimals = target_decimals
        };
    }

    // Evaluates triangular arbitrage disparity against direct market quote
    static TriangularArbOpportunity evaluate_arbitrage(
        const FxQuote& direct_cross, // Direct EUR/JPY
        const FxQuote& pair_a,       // EUR/USD
        const FxQuote& pair_b,       // USD/JPY
        double min_profit_bps = 2.0) noexcept
    {
        TriangularArbOpportunity opp;
        auto synthetic = compute_synthetic_cross(pair_a, pair_b, direct_cross.decimals);
        opp.implied_cross_bid = synthetic.bid_price;
        opp.implied_cross_ask = synthetic.ask_price;

        double d_factor = std::pow(10.0, direct_cross.decimals);
        double direct_bid = static_cast<double>(direct_cross.bid_price) / d_factor;
        double direct_ask = static_cast<double>(direct_cross.ask_price) / d_factor;

        double synth_bid = static_cast<double>(synthetic.bid_price) / d_factor;
        double synth_ask = static_cast<double>(synthetic.ask_price) / d_factor;

        // Path 1: Buy Synthetic (EUR/USD + USD/JPY), Sell Direct EUR/JPY
        // Profit if direct_bid > synth_ask
        if (direct_bid > synth_ask && direct_bid > 0.0) {
            double profit_bps = ((direct_bid - synth_ask) / direct_bid) * 10000.0;
            if (profit_bps >= min_profit_bps) {
                opp.detected = true;
                opp.profit_bps = profit_bps;
                opp.buy_synthetic = true;
                return opp;
            }
        }

        // Path 2: Buy Direct EUR/JPY, Sell Synthetic (EUR/USD + USD/JPY)
        // Profit if synth_bid > direct_ask
        if (synth_bid > direct_ask && synth_bid > 0.0) {
            double profit_bps = ((synth_bid - direct_ask) / synth_bid) * 10000.0;
            if (profit_bps >= min_profit_bps) {
                opp.detected = true;
                opp.profit_bps = profit_bps;
                opp.buy_synthetic = false;
                return opp;
            }
        }

        return opp;
    }
};

} // namespace fx
} // namespace luv
