#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {

struct OHLCBar {
    uint64_t timestamp_ns{0};
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    uint64_t volume{0};
};

struct ExtremeValueVolatilityResult {
    double parkinson_vol{0.0};
    double garman_klass_vol{0.0};
    double rogers_satchell_vol{0.0};
    double yang_zhang_vol{0.0};
    double close_to_close_vol{0.0};
    size_t sample_count{0};
};

class ExtremeValueVolatilityEngine {
public:
    static constexpr size_t MAX_BARS = 256;

    ExtremeValueVolatilityEngine() noexcept {
        reset();
    }

    void reset() noexcept {
        bar_count_ = 0;
    }

    bool add_bar(const OHLCBar& bar) noexcept {
        if (bar.open <= 0.0 || bar.high <= 0.0 || bar.low <= 0.0 || bar.close <= 0.0) return false;
        if (bar.high < bar.low || bar.high < bar.open || bar.high < bar.close) return false;
        if (bar.low > bar.open || bar.low > bar.close) return false;

        if (bar_count_ >= MAX_BARS) {
            for (size_t i = 1; i < MAX_BARS; ++i) {
                bars_[i - 1] = bars_[i];
            }
            bars_[MAX_BARS - 1] = bar;
            return true;
        }

        bars_[bar_count_++] = bar;
        return true;
    }

    ExtremeValueVolatilityResult calculate_volatilities() const noexcept {
        ExtremeValueVolatilityResult res{};
        res.sample_count = bar_count_;
        if (bar_count_ < 2) return res;

        double n = static_cast<double>(bar_count_);
        double parkinson_sum = 0.0;
        double gk_sum = 0.0;
        double rs_sum = 0.0;

        constexpr double C_PARKINSON = 1.0 / (4.0 * 0.6931471805599453); // 1 / (4 ln 2)
        constexpr double C_GK_CLOSE = 2.0 * 0.6931471805599453 - 1.0;   // 2 ln 2 - 1

        for (size_t i = 0; i < bar_count_; ++i) {
            const auto& b = bars_[i];
            double log_hl = std::log(b.high / b.low);
            double log_co = std::log(b.close / b.open);
            double log_hc = std::log(b.high / b.close);
            double log_ho = std::log(b.high / b.open);
            double log_lc = std::log(b.low / b.close);
            double log_lo = std::log(b.low / b.open);

            // Parkinson: log(H/L)^2
            parkinson_sum += log_hl * log_hl;

            // Garman-Klass: 0.5 * log(H/L)^2 - (2 ln 2 - 1) * log(C/O)^2
            gk_sum += (0.5 * log_hl * log_hl) - (C_GK_CLOSE * log_co * log_co);

            // Rogers-Satchell: log(H/C)*log(H/O) + log(L/C)*log(L/O)
            rs_sum += (log_hc * log_ho) + (log_lc * log_lo);
        }

        double var_parkinson = C_PARKINSON * (parkinson_sum / n);
        double var_gk = std::max(0.0, gk_sum / n);
        double var_rs = std::max(0.0, rs_sum / n);

        res.parkinson_vol = std::sqrt(std::max(0.0, var_parkinson));
        res.garman_klass_vol = std::sqrt(std::max(0.0, var_gk));
        res.rogers_satchell_vol = std::sqrt(std::max(0.0, var_rs));

        // Close-to-Close Volatility
        double cc_sum = 0.0;
        double cc_sq_sum = 0.0;
        size_t cc_count = bar_count_ - 1;
        for (size_t i = 1; i < bar_count_; ++i) {
            double ret = std::log(bars_[i].close / bars_[i - 1].close);
            cc_sum += ret;
            cc_sq_sum += ret * ret;
        }
        double mean_ret = cc_sum / static_cast<double>(cc_count);
        double var_cc = (cc_sq_sum / static_cast<double>(cc_count)) - (mean_ret * mean_ret);
        res.close_to_close_vol = std::sqrt(std::max(0.0, var_cc));

        // Yang-Zhang Volatility
        // Overnight return: log(Open_t / Close_{t-1})
        double on_sum = 0.0;
        double on_sq_sum = 0.0;
        // Open-to-close return: log(Close_t / Open_t)
        double oc_sum = 0.0;
        double oc_sq_sum = 0.0;
        for (size_t i = 1; i < bar_count_; ++i) {
            double r_on = std::log(bars_[i].open / bars_[i - 1].close);
            on_sum += r_on;
            on_sq_sum += r_on * r_on;

            double r_oc = std::log(bars_[i].close / bars_[i].open);
            oc_sum += r_oc;
            oc_sq_sum += r_oc * r_oc;
        }
        double var_on = (on_sq_sum / static_cast<double>(cc_count)) - (on_sum / static_cast<double>(cc_count)) * (on_sum / static_cast<double>(cc_count));
        double var_oc = (oc_sq_sum / static_cast<double>(cc_count)) - (oc_sum / static_cast<double>(cc_count)) * (oc_sum / static_cast<double>(cc_count));

        double k_param = 0.34 / (1.34 + ((n + 1.0) / (n - 1.0)));
        double var_yz = std::max(0.0, var_on + (k_param * var_oc) + ((1.0 - k_param) * var_rs));
        res.yang_zhang_vol = std::sqrt(var_yz);

        return res;
    }

    size_t bar_count() const noexcept { return bar_count_; }

private:
    std::array<OHLCBar, MAX_BARS> bars_{};
    size_t bar_count_{0};
};

} // namespace luv
