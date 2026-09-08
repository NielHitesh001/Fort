#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace luv {
namespace crypto {

struct ConcentratedPosition {
    uint64_t position_id = 0;
    double lower_tick_price = 0.0;
    double upper_tick_price = 0.0;
    double liquidity = 0.0; // L
    double accrued_fee_token0 = 0.0;
    double accrued_fee_token1 = 0.0;
};

struct AmmSwapResult {
    double amount_in = 0.0;
    double amount_out = 0.0;
    double effective_price = 0.0;
    double price_impact_bps = 0.0;
    double fee_paid = 0.0;
};

class AmmLiquidityEngine {
public:
    static constexpr double kDefaultFeeTier = 0.0030; // 0.30% (30 bps)

    explicit AmmLiquidityEngine(double reserve0 = 1'000'000.0, double reserve1 = 1'000'000.0, double fee = kDefaultFeeTier) noexcept
        : r0_(reserve0), r1_(reserve1), fee_tier_(fee) {}

    // Constant Product Market Maker (x * y = k) swap token0 for token1
    AmmSwapResult swap_token0_for_token1(double amount0_in) noexcept {
        AmmSwapResult res{};
        if (amount0_in <= 0.0 || r0_ <= 0.0 || r1_ <= 0.0) return res;

        double initial_spot_price = r1_ / r0_; // Price of token0 in terms of token1
        double fee = amount0_in * fee_tier_;
        double net_amount_in = amount0_in - fee;

        // dy = y - (k / (x + net_dx)) = (y * net_dx) / (x + net_dx)
        double amount1_out = (r1_ * net_amount_in) / (r0_ + net_amount_in);

        r0_ += amount0_in;
        r1_ -= amount1_out;

        double exec_price = amount1_out / amount0_in;
        double impact = ((initial_spot_price - exec_price) / initial_spot_price) * 10000.0;

        res.amount_in = amount0_in;
        res.amount_out = amount1_out;
        res.effective_price = exec_price;
        res.price_impact_bps = std::max(0.0, impact);
        res.fee_paid = fee;

        return res;
    }

    double get_spot_price() const noexcept {
        if (r0_ <= 0.0) return 0.0;
        return r1_ / r0_;
    }

    double get_reserve0() const noexcept { return r0_; }
    double get_reserve1() const noexcept { return r1_; }

private:
    double r0_{1'000'000.0};
    double r1_{1'000'000.0};
    double fee_tier_{0.0030};
};

} // namespace crypto
} // namespace luv
