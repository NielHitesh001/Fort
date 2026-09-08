#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace crypto {

struct SpotIndexConstituent {
    uint8_t exchange_id = 0;
    int64_t spot_price = 0; // Scaled x10,000
    double weight = 0.25;
    bool active = true;
};

class FairMarkPriceEngine {
public:
    static constexpr size_t kMaxSpotVenues = 8;
    static constexpr int64_t kMaxBasisClamp = 50'0000LL; // +/- $50 basis clamp

    FairMarkPriceEngine() noexcept : num_venues_(0), last_ema_basis_(0) {}

    bool add_spot_venue(uint8_t venue_id, double weight) noexcept {
        if (num_venues_ >= kMaxSpotVenues || weight <= 0.0) return false;
        venues_[num_venues_++] = SpotIndexConstituent{venue_id, 0, weight, true};
        return true;
    }

    bool update_spot_price(uint8_t venue_id, int64_t price) noexcept {
        for (size_t i = 0; i < num_venues_; ++i) {
            if (venues_[i].exchange_id == venue_id) {
                venues_[i].spot_price = price;
                return true;
            }
        }
        return false;
    }

    // Computes Composite Spot Index Price across active exchanges
    int64_t compute_index_price() const noexcept {
        if (num_venues_ == 0) return 0;

        double sum_weighted_price = 0.0;
        double sum_weights = 0.0;

        for (size_t i = 0; i < num_venues_; ++i) {
            if (venues_[i].active && venues_[i].spot_price > 0) {
                sum_weighted_price += static_cast<double>(venues_[i].spot_price) * venues_[i].weight;
                sum_weights += venues_[i].weight;
            }
        }

        if (sum_weights <= 0.0) return 0;
        return static_cast<int64_t>(std::round(sum_weighted_price / sum_weights));
    }

    // Computes robust Fair Mark Price: Mark = IndexPrice + Median(0, BookBasis, EMA_Basis)
    int64_t compute_fair_mark_price(int64_t orderbook_mid_price) noexcept {
        int64_t index_price = compute_index_price();
        if (index_price <= 0) return orderbook_mid_price;

        int64_t book_basis = orderbook_mid_price - index_price;
        // Clamp book basis
        book_basis = std::clamp(book_basis, -kMaxBasisClamp, kMaxBasisClamp);

        // Update 30-second basis EMA (alpha = 0.1)
        last_ema_basis_ = static_cast<int64_t>(0.9 * static_cast<double>(last_ema_basis_) + 0.1 * static_cast<double>(book_basis));

        // Median of 3 elements: [0, book_basis, last_ema_basis_]
        std::array<int64_t, 3> basis_vals = {0, book_basis, last_ema_basis_};
        std::sort(basis_vals.begin(), basis_vals.end());
        int64_t fair_basis = basis_vals[1]; // middle element

        return index_price + fair_basis;
    }

private:
    std::array<SpotIndexConstituent, kMaxSpotVenues> venues_{};
    size_t num_venues_{0};
    int64_t last_ema_basis_{0};
};

} // namespace crypto
} // namespace luv
