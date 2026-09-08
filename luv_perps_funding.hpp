#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace crypto {

struct FundingSample {
    uint64_t timestamp_ns = 0;
    int64_t mark_price = 0;  // Scaled x10,000
    int64_t index_price = 0; // Scaled x10,000
};

class PerpetualFundingEngine {
public:
    static constexpr size_t kMaxSamples = 480; // 8-hour window @ 1 minute samples
    static constexpr double kMaxFundingRateClamp = 0.0075; // +/- 0.75% max 8h funding rate
    static constexpr double kInterestRateComponent = 0.0001; // 0.01% baseline 8h interest rate

    PerpetualFundingEngine() noexcept : num_samples_(0) {}

    bool record_price_sample(uint64_t timestamp_ns, int64_t mark_price, int64_t index_price) noexcept {
        if (mark_price <= 0 || index_price <= 0 || num_samples_ >= kMaxSamples) return false;
        samples_[num_samples_++] = FundingSample{timestamp_ns, mark_price, index_price};
        return true;
    }

    // Computes the 8-hour Premium Index TWAP and resulting clamped funding rate
    double compute_funding_rate() const noexcept {
        if (num_samples_ == 0) return 0.0;

        double sum_premium = 0.0;
        for (size_t i = 0; i < num_samples_; ++i) {
            double p_mark = static_cast<double>(samples_[i].mark_price);
            double p_index = static_cast<double>(samples_[i].index_price);
            double premium_fraction = (p_mark - p_index) / p_index;
            sum_premium += premium_fraction;
        }

        double twap_premium = sum_premium / static_cast<double>(num_samples_);

        // Funding rate F = Premium + clamp(InterestRate - Premium, -0.05%, +0.05%)
        double diff = kInterestRateComponent - twap_premium;
        double clamped_diff = std::clamp(diff, -0.0005, 0.0005);
        double raw_funding = twap_premium + clamped_diff;

        return std::clamp(raw_funding, -kMaxFundingRateClamp, kMaxFundingRateClamp);
    }

    // Computes funding payment for a given position size (in contracts/base currency) and settlement price
    int64_t compute_funding_payment(int64_t position_qty, int64_t mark_price, double funding_rate) const noexcept {
        // Payment = Position * Mark Price * FundingRate
        // Long pays Short when FundingRate > 0
        double payment = static_cast<double>(position_qty) * (static_cast<double>(mark_price) / 10000.0) * funding_rate;
        return static_cast<int64_t>(std::round(payment * 10000.0));
    }

    void reset() noexcept { num_samples_ = 0; }

private:
    std::array<FundingSample, kMaxSamples> samples_{};
    size_t num_samples_{0};
};

} // namespace crypto
} // namespace luv
