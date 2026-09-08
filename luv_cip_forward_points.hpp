#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>
#include <string_view>

namespace luv {

struct CIPMarketInput {
    char ccy_pair[8]{0};       // e.g. "EURUSD"
    double spot_price{0.0};    // e.g. 1.0850
    double domestic_rate{0.0}; // r_d (e.g. USD SOFR rate 0.0525)
    double foreign_rate{0.0};  // r_f (e.g. EUR ESTER rate 0.0375)
    uint32_t days_to_maturity{90};
    double market_forward_points{0.0}; // in pips (1 pip = 0.0001)
    double bid_ask_spread_pips{0.5};
    double haircut_rate{0.0002}; // 2 bp funding haircut
};

struct CIPAnalysisResult {
    char ccy_pair[8]{0};
    double spot_price{0.0};
    double theoretical_forward_price{0.0};
    double theoretical_forward_points{0.0};
    double market_forward_price{0.0};
    double basis_spread_bp{0.0};       // Cross-currency basis kappa in bps
    double net_arbitrage_profit_bp{0.0};
    bool arbitrage_opportunity{false};
    bool borrow_domestic_lend_foreign{false}; // Synthetic foreign currency lending
};

struct CIPConfig {
    double min_arbitrage_threshold_bp{1.5}; // Minimum 1.5 bp dislocation
};

class CIPForwardPointsEngine {
public:
    explicit CIPForwardPointsEngine(const CIPConfig& cfg = CIPConfig{}) noexcept
        : config_(cfg) {}

    CIPAnalysisResult analyze_parity(const CIPMarketInput& input) const noexcept {
        CIPAnalysisResult result{};
        std::strncpy(result.ccy_pair, input.ccy_pair, sizeof(result.ccy_pair) - 1);
        result.spot_price = input.spot_price;

        if (input.spot_price <= 0.0 || input.days_to_maturity == 0) {
            return result;
        }

        double year_fraction = static_cast<double>(input.days_to_maturity) / 360.0;
        double denom = 1.0 + (input.foreign_rate * year_fraction);
        if (denom <= 1e-9) return result;

        double numer = 1.0 + (input.domestic_rate * year_fraction);
        double f_theo = input.spot_price * (numer / denom);
        double f_theo_points = (f_theo - input.spot_price) * 10'000.0; // In standard pips

        double f_mkt = input.spot_price + (input.market_forward_points / 10'000.0);

        result.theoretical_forward_price = f_theo;
        result.theoretical_forward_points = f_theo_points;
        result.market_forward_price = f_mkt;

        // Calculate implied cross-currency basis spread kappa:
        // (F_mkt / S) * (1 + r_f * yf) - 1 = (r_d + kappa) * yf
        double ratio = f_mkt / input.spot_price;
        double implied_total_dom_rate = (ratio * denom - 1.0) / year_fraction;
        double kappa = (implied_total_dom_rate - input.domestic_rate) * 10'000.0; // In bps
        result.basis_spread_bp = kappa;

        // Arbitrage calculation taking into account bid-ask spread and haircut
        double total_friction_bp = (input.bid_ask_spread_pips * 1.0) + (input.haircut_rate * 10'000.0);
        double abs_dislocation = std::abs(kappa);

        if (abs_dislocation > total_friction_bp + config_.min_arbitrage_threshold_bp) {
            result.arbitrage_opportunity = true;
            result.net_arbitrage_profit_bp = abs_dislocation - total_friction_bp;
            // If kappa < 0 (market forward points underpriced), borrow USD, buy Spot EUR, lend EUR, sell Forward EUR
            result.borrow_domestic_lend_foreign = (kappa < 0.0);
        }

        return result;
    }

private:
    CIPConfig config_{};
};

} // namespace luv
