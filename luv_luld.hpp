#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace luv {

enum class LuldTier : uint8_t {
    Tier1 = 1, // S&P 500, Russell 1000, select ETPs (5% or 10% depending on price level)
    Tier2 = 2  // Other NMS stocks (10% or 20% depending on price level)
};

enum class LuldState : uint8_t {
    Normal = 0,
    Straddle = 1,
    LimitState = 2,
    TradingPause = 3
};

struct LuldConfig {
    LuldTier tier{LuldTier::Tier1};
    uint64_t straddle_timeout_ns{15'000'000'000ULL}; // 15 seconds
    bool is_market_open_or_close_period{false};     // Double percentages during open (9:30-9:45) & close (3:35-4:00)
};

struct LuldBands {
    uint64_t reference_price{0};
    uint64_t lower_band{0};
    uint64_t upper_band{0};
    LuldState state{LuldState::Normal};
    uint64_t straddle_start_ns{0};
    bool pause_triggered{false};
};

class LuldEngine {
public:
    explicit LuldEngine(const LuldConfig& config = {}) : config_(config) {}

    void set_reference_price(uint64_t ref_price) {
        bands_.reference_price = ref_price;
        recalculate_bands();
    }

    void update_nbbo(uint64_t best_bid, uint64_t best_ask, uint64_t now_ns) {
        if (bands_.reference_price == 0) return;

        bool bid_at_upper = (best_bid >= bands_.upper_band);
        bool ask_at_lower = (best_ask <= bands_.lower_band && best_ask > 0);

        if (bands_.state == LuldState::TradingPause) {
            return;
        }

        if (bid_at_upper || ask_at_lower) {
            if (bands_.state != LuldState::LimitState && bands_.state != LuldState::Straddle) {
                bands_.state = LuldState::LimitState;
                bands_.straddle_start_ns = now_ns;
            } else if (now_ns >= bands_.straddle_start_ns + config_.straddle_timeout_ns) {
                bands_.state = LuldState::TradingPause;
                bands_.pause_triggered = true;
            }
        } else if (best_bid > bands_.lower_band && best_ask < bands_.upper_band) {
            bands_.state = LuldState::Normal;
            bands_.straddle_start_ns = 0;
        } else {
            if (bands_.state == LuldState::Normal) {
                bands_.state = LuldState::Straddle;
                bands_.straddle_start_ns = now_ns;
            } else if (bands_.state == LuldState::Straddle && now_ns >= bands_.straddle_start_ns + config_.straddle_timeout_ns) {
                bands_.state = LuldState::TradingPause;
                bands_.pause_triggered = true;
            }
        }
    }

    bool is_trade_allowed(uint64_t trade_price) const {
        if (bands_.state == LuldState::TradingPause) return false;
        if (bands_.reference_price == 0) return true;
        return (trade_price >= bands_.lower_band && trade_price <= bands_.upper_band);
    }

    void resume_trading(uint64_t new_ref_price) {
        bands_.state = LuldState::Normal;
        bands_.pause_triggered = false;
        bands_.straddle_start_ns = 0;
        set_reference_price(new_ref_price);
    }

    const LuldBands& get_bands() const noexcept { return bands_; }

private:
    void recalculate_bands() {
        if (bands_.reference_price == 0) {
            bands_.lower_band = 0;
            bands_.upper_band = 0;
            return;
        }

        uint64_t bp = 500; // default 5% (500 bps)
        uint64_t p = bands_.reference_price; // scaled by 10,000

        if (config_.tier == LuldTier::Tier1) {
            if (p > 30000) bp = 500;
            else if (p >= 7500) bp = 2000;
            else bp = 7500;
        } else {
            if (p > 30000) bp = 1000;
            else if (p >= 7500) bp = 2000;
            else bp = 7500;
        }

        if (config_.is_market_open_or_close_period) {
            bp *= 2;
        }

        uint64_t delta = (p * bp) / 10000;
        if (p <= 7500 && delta > 1500) {
            delta = 1500;
        }

        bands_.lower_band = (p > delta) ? (p - delta) : 0;
        bands_.upper_band = p + delta;
    }

    LuldConfig config_;
    LuldBands bands_;
};

} // namespace luv
