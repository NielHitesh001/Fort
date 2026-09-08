#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace routing {

struct VenueQuote {
    uint8_t venue_id = 0;
    int64_t bid_price = 0;
    int64_t ask_price = 0;
    int64_t bid_qty = 0;
    int64_t ask_qty = 0;
    uint64_t latency_to_venue_ns = 0;
};

struct RoutedSliceResult {
    uint8_t venue_id = 0;
    int64_t routed_qty = 0;
    int64_t executed_qty = 0;
    int64_t executed_price = 0;
};

class SmartOrderRoutingSimulator {
public:
    static constexpr size_t kMaxVenues = 8;

    SmartOrderRoutingSimulator() noexcept : num_venues_(0) {}

    bool update_venue(const VenueQuote& quote) noexcept {
        for (size_t i = 0; i < num_venues_; ++i) {
            if (venues_[i].venue_id == quote.venue_id) {
                venues_[i] = quote;
                return true;
            }
        }
        if (num_venues_ >= kMaxVenues) return false;
        venues_[num_venues_++] = quote;
        return true;
    }

    // Routes order across venues prioritized by price, available size, and venue transit latency
    size_t route_order(uint8_t side, int64_t total_qty, RoutedSliceResult* out_slices, size_t max_slices) noexcept {
        if (!out_slices || max_slices == 0 || total_qty <= 0 || num_venues_ == 0) return 0;

        // Copy and sort venues by best price
        std::array<VenueQuote, kMaxVenues> sorted_venues = venues_;
        size_t n = num_venues_;

        if (side == exec::kBuy) {
            // Sort ascending ask price
            std::sort(sorted_venues.begin(), sorted_venues.begin() + n,
                [](const VenueQuote& a, const VenueQuote& b) {
                    if (a.ask_price != b.ask_price) return a.ask_price < b.ask_price;
                    return a.latency_to_venue_ns < b.latency_to_venue_ns;
                });
        } else {
            // Sort descending bid price
            std::sort(sorted_venues.begin(), sorted_venues.begin() + n,
                [](const VenueQuote& a, const VenueQuote& b) {
                    if (a.bid_price != b.bid_price) return a.bid_price > b.bid_price;
                    return a.latency_to_venue_ns < b.latency_to_venue_ns;
                });
        }

        int64_t remaining_qty = total_qty;
        size_t slice_count = 0;

        for (size_t i = 0; i < n && remaining_qty > 0 && slice_count < max_slices; ++i) {
            const auto& v = sorted_venues[i];
            int64_t avail_qty = (side == exec::kBuy) ? v.ask_qty : v.bid_qty;
            int64_t exec_price = (side == exec::kBuy) ? v.ask_price : v.bid_price;

            if (avail_qty <= 0 || exec_price <= 0) continue;

            int64_t to_route = std::min(remaining_qty, avail_qty);

            out_slices[slice_count++] = RoutedSliceResult{
                .venue_id = v.venue_id,
                .routed_qty = to_route,
                .executed_qty = to_route,
                .executed_price = exec_price
            };

            remaining_qty -= to_route;
        }

        return slice_count;
    }

private:
    std::array<VenueQuote, kMaxVenues> venues_{};
    size_t num_venues_{0};
};

} // namespace routing
} // namespace luv
