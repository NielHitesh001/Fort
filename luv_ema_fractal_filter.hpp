#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {

struct EMARibbonState {
    std::array<double, 6> emas{}; // Horizons: 5, 8, 13, 21, 34, 55
    int32_t alignment_score{0};   // +5 (strong bull) to -5 (strong bear)
    double ribbon_spread_pct{0.0};
    double fractal_dimension{1.5};// 1.0 (pure trend) to 1.7+ (extreme noise)
    bool is_trending{false};
    bool is_ranging{false};
};

class EMAFractalFilterEngine {
public:
    static constexpr size_t MAX_WINDOW = 64;
    static constexpr std::array<size_t, 6> HORIZONS{5, 8, 13, 21, 34, 55};

    EMAFractalFilterEngine() noexcept {
        reset();
    }

    void reset() noexcept {
        initialized_ = false;
        point_count_ = 0;
        for (size_t k = 0; k < 6; ++k) {
            emas_[k] = 0.0;
        }
    }

    EMARibbonState update(double price) noexcept {
        EMARibbonState state{};
        if (price <= 0.0) return state;

        if (!initialized_) {
            for (size_t k = 0; k < 6; ++k) {
                emas_[k] = price;
            }
            initialized_ = true;
        } else {
            for (size_t k = 0; k < 6; ++k) {
                double alpha = 2.0 / (static_cast<double>(HORIZONS[k]) + 1.0);
                emas_[k] = (alpha * price) + ((1.0 - alpha) * emas_[k]);
            }
        }

        // Store rolling price window for fractal dimension
        if (point_count_ >= MAX_WINDOW) {
            for (size_t i = 1; i < MAX_WINDOW; ++i) {
                window_[i - 1] = window_[i];
            }
            window_[MAX_WINDOW - 1] = price;
        } else {
            window_[point_count_++] = price;
        }

        state.emas = emas_;

        // Compute alignment score
        int32_t score = 0;
        for (size_t k = 0; k < 5; ++k) {
            if (emas_[k] > emas_[k + 1]) score += 1;
            else if (emas_[k] < emas_[k + 1]) score -= 1;
        }
        state.alignment_score = score;

        if (emas_[5] > 0.0) {
            state.ribbon_spread_pct = ((emas_[0] - emas_[5]) / emas_[5]) * 100.0;
        }

        // Compute Sevcik Fractal Dimension over rolling window
        state.fractal_dimension = compute_sevcik_dimension();
        state.is_trending = (state.fractal_dimension < 1.35) && (std::abs(score) >= 4);
        state.is_ranging = (state.fractal_dimension >= 1.50) || (std::abs(score) <= 1);

        return state;
    }

private:
    double compute_sevcik_dimension() const noexcept {
        if (point_count_ < 8) return 1.5;

        double min_p = window_[0];
        double max_p = window_[0];
        for (size_t i = 1; i < point_count_; ++i) {
            if (window_[i] < min_p) min_p = window_[i];
            if (window_[i] > max_p) max_p = window_[i];
        }

        double range = max_p - min_p;
        if (range <= 1e-9) return 1.0;

        size_t n = point_count_;
        double n_prime = static_cast<double>(n - 1);
        double total_length = 0.0;

        for (size_t i = 0; i < n - 1; ++i) {
            double y_i = (window_[i] - min_p) / range;
            double y_next = (window_[i + 1] - min_p) / range;
            double dx = 1.0 / n_prime;
            double dy = y_next - y_i;
            total_length += std::sqrt(dx * dx + dy * dy);
        }

        if (total_length <= 0.0) return 1.0;

        // Sevcik formula: D = 1 + (ln(L) + ln(2)) / ln(2 * N')
        double d = 1.0 + (std::log(total_length) + std::log(2.0)) / std::log(2.0 * n_prime);
        return std::clamp(d, 1.0, 2.0);
    }

    bool initialized_{false};
    std::array<double, 6> emas_{};
    std::array<double, MAX_WINDOW> window_{};
    size_t point_count_{0};
};

} // namespace luv
