#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace luv {

enum class MarketRegime : uint8_t {
    Calm = 0,      // Low volatility, tight spreads, normal inventory capacity
    Normal = 1,    // Baseline volatility
    Stressed = 2,  // Elevated volatility, wider spreads, reduced inventory
    Crisis = 3     // Flash crash / circuit breaker regime, maximum spread, minimum inventory
};

struct RegimeParams {
    double calm_spread_mult{0.8};
    double normal_spread_mult{1.0};
    double stressed_spread_mult{2.0};
    double crisis_spread_mult{5.0};

    int64_t calm_max_inventory{1000};
    int64_t normal_max_inventory{500};
    int64_t stressed_max_inventory{200};
    int64_t crisis_max_inventory{50};

    double ema_alpha{0.1}; // Volatility EMA smoothing factor
    double stress_vol_threshold{0.03}; // 3% volatility
    double crisis_vol_threshold{0.08}; // 8% volatility
};

struct AdaptiveQuote {
    int64_t bid_price{0};
    int64_t ask_price{0};
    int64_t spread{0};
    int64_t max_allowed_inventory{0};
    MarketRegime current_regime{MarketRegime::Normal};
};

class RegimeSwitchingQuoter {
public:
    explicit RegimeSwitchingQuoter(const RegimeParams& params = {}) noexcept
        : params_(params), current_vol_ema_(0.015), regime_(MarketRegime::Normal) {}

    // Update with latest realized price tick variance
    void on_price_update(uint64_t last_price, uint64_t prev_price) noexcept {
        if (prev_price == 0 || last_price == 0) return;

        double ret = std::abs(static_cast<double>(last_price) - static_cast<double>(prev_price)) / static_cast<double>(prev_price);
        current_vol_ema_ = params_.ema_alpha * ret + (1.0 - params_.ema_alpha) * current_vol_ema_;

        if (current_vol_ema_ >= params_.crisis_vol_threshold) {
            regime_ = MarketRegime::Crisis;
        } else if (current_vol_ema_ >= params_.stress_vol_threshold) {
            regime_ = MarketRegime::Stressed;
        } else if (current_vol_ema_ <= 0.005) {
            regime_ = MarketRegime::Calm;
        } else {
            regime_ = MarketRegime::Normal;
        }
    }

    void force_regime(MarketRegime r) noexcept {
        regime_ = r;
    }

    AdaptiveQuote compute_adaptive_quote(
        int64_t mid_price,
        int64_t base_half_spread,
        int64_t inventory) const noexcept
    {
        AdaptiveQuote q;
        q.current_regime = regime_;

        double spread_mult = params_.normal_spread_mult;
        int64_t max_inv = params_.normal_max_inventory;

        switch (regime_) {
            case MarketRegime::Calm:
                spread_mult = params_.calm_spread_mult;
                max_inv = params_.calm_max_inventory;
                break;
            case MarketRegime::Normal:
                spread_mult = params_.normal_spread_mult;
                max_inv = params_.normal_max_inventory;
                break;
            case MarketRegime::Stressed:
                spread_mult = params_.stressed_spread_mult;
                max_inv = params_.stressed_max_inventory;
                break;
            case MarketRegime::Crisis:
                spread_mult = params_.crisis_spread_mult;
                max_inv = params_.crisis_max_inventory;
                break;
        }

        q.max_allowed_inventory = max_inv;

        int64_t effective_half_spread = static_cast<int64_t>(std::round(static_cast<double>(base_half_spread) * spread_mult));
        effective_half_spread = std::max(int64_t{1}, effective_half_spread);

        // Skew based on inventory position
        int64_t inventory_skew = (inventory * effective_half_spread) / max_inv;

        q.bid_price = mid_price - effective_half_spread - inventory_skew;
        q.ask_price = mid_price + effective_half_spread - inventory_skew;
        q.spread = q.ask_price - q.bid_price;

        return q;
    }

    MarketRegime get_regime() const noexcept { return regime_; }
    double get_current_vol_ema() const noexcept { return current_vol_ema_; }

private:
    RegimeParams params_;
    double current_vol_ema_{0.015};
    MarketRegime regime_{MarketRegime::Normal};
};

} // namespace luv
