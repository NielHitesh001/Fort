#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace risk {

struct CorrelatedProductOffset {
    uint16_t product_a_id = 0; // e.g. SPX Futures
    uint16_t product_b_id = 0; // e.g. SPY ETF
    double correlation_credit_pct = 0.80; // 80% margin relief on offsetting positions
};

struct AssetMarginPosition {
    uint16_t product_id = 0;
    int64_t standalone_margin_req = 0; // Scaled x10,000
    int64_t net_delta_notional = 0;    // Positive = long delta, Negative = short delta
};

class CrossAssetMarginOptimizer {
public:
    static constexpr size_t kMaxProducts = 32;
    static constexpr size_t kMaxOffsets = 16;

    CrossAssetMarginOptimizer() noexcept : num_products_(0), num_offsets_(0) {}

    bool register_product_position(uint16_t product_id, int64_t standalone_margin, int64_t net_delta) noexcept {
        if (num_products_ >= kMaxProducts) return false;
        products_[num_products_++] = AssetMarginPosition{product_id, standalone_margin, net_delta};
        return true;
    }

    bool register_correlation_offset(uint16_t prod_a, uint16_t prod_b, double credit_pct) noexcept {
        if (num_offsets_ >= kMaxOffsets || credit_pct <= 0.0 || credit_pct > 1.0) return false;
        offsets_[num_offsets_++] = CorrelatedProductOffset{prod_a, prod_b, credit_pct};
        return true;
    }

    // Computes aggregate portfolio margin after applying cross-product correlation offsets
    int64_t compute_optimized_portfolio_margin() const noexcept {
        if (num_products_ == 0) return 0;

        int64_t total_standalone_margin = 0;
        for (size_t i = 0; i < num_products_; ++i) {
            total_standalone_margin += products_[i].standalone_margin_req;
        }

        int64_t total_offset_relief = 0;

        for (size_t k = 0; k < num_offsets_; ++k) {
            const auto& off = offsets_[k];
            const AssetMarginPosition* pos_a = nullptr;
            const AssetMarginPosition* pos_b = nullptr;

            for (size_t i = 0; i < num_products_; ++i) {
                if (products_[i].product_id == off.product_a_id) pos_a = &products_[i];
                if (products_[i].product_id == off.product_b_id) pos_b = &products_[i];
            }

            if (!pos_a || !pos_b) continue;

            // Offsetting check: Pos A and Pos B have opposite delta signs (one long, one short)
            if ((pos_a->net_delta_notional > 0 && pos_b->net_delta_notional < 0) ||
                (pos_a->net_delta_notional < 0 && pos_b->net_delta_notional > 0)) {
                
                int64_t max_offset_base = std::min(pos_a->standalone_margin_req, pos_b->standalone_margin_req);
                int64_t relief = static_cast<int64_t>(max_offset_base * off.correlation_credit_pct);
                total_offset_relief += relief;
            }
        }

        return std::max<int64_t>(0, total_standalone_margin - total_offset_relief);
    }

private:
    std::array<AssetMarginPosition, kMaxProducts> products_{};
    size_t num_products_{0};
    std::array<CorrelatedProductOffset, kMaxOffsets> offsets_{};
    size_t num_offsets_{0};
};

} // namespace risk
} // namespace luv
