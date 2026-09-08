#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "luv_execution.hpp"

namespace luv {

enum class VenueId : uint8_t {
    kNasdaq = 0,
    kNyse = 1,
    kBats = 2,
    kIex = 3,
    kCme = 4,
    kMaxVenues = 5,
};

struct VenueQuote {
    VenueId venue = VenueId::kNasdaq;
    int64_t bid_price = 0;
    int64_t bid_qty = 0;
    int64_t ask_price = 0;
    int64_t ask_qty = 0;
    uint32_t fee_bps = 10;      // 0.10 bps maker/taker fee
    uint32_t latency_ns = 500;  // estimated round-trip latency
};

struct VenueRouteSlice {
    VenueId venue = VenueId::kNasdaq;
    int64_t price = 0;
    int64_t qty = 0;
    uint32_t client_order_id = 0;
};

class SmartOrderRouter {
public:
    static constexpr uint8_t kMaxVenues = static_cast<uint8_t>(VenueId::kMaxVenues);

    SmartOrderRouter() noexcept {
        for (uint8_t v = 0; v < kMaxVenues; ++v) {
            _quotes[v].venue = static_cast<VenueId>(v);
        }
    }

    void update_venue_quote(VenueId venue, int64_t bid_price, int64_t bid_qty,
                            int64_t ask_price, int64_t ask_qty,
                            uint32_t fee_bps = 10, uint32_t latency_ns = 500) noexcept {
        const uint8_t idx = static_cast<uint8_t>(venue);
        if (idx < kMaxVenues) {
            _quotes[idx].bid_price = bid_price;
            _quotes[idx].bid_qty = bid_qty;
            _quotes[idx].ask_price = ask_price;
            _quotes[idx].ask_qty = ask_qty;
            _quotes[idx].fee_bps = fee_bps;
            _quotes[idx].latency_ns = latency_ns;
        }
    }

    [[nodiscard]] std::vector<VenueRouteSlice> route_order(
        uint8_t side, int64_t target_qty, int64_t limit_price,
        uint32_t base_client_id = 1000) const noexcept {

        std::vector<VenueRouteSlice> slices;
        if (target_qty <= 0 || limit_price <= 0) return slices;

        struct RankedVenue {
            VenueId venue;
            int64_t available_price;
            int64_t available_qty;
            uint32_t fee_bps;
        };

        std::array<RankedVenue, kMaxVenues> candidates{};
        uint8_t candidate_count = 0;

        for (uint8_t i = 0; i < kMaxVenues; ++i) {
            const auto& q = _quotes[i];
            if (side == exec::kBuy) {
                if (q.ask_price > 0 && q.ask_price <= limit_price && q.ask_qty > 0) {
                    candidates[candidate_count++] = {q.venue, q.ask_price, q.ask_qty, q.fee_bps};
                }
            } else { // Sell
                if (q.bid_price >= limit_price && q.bid_qty > 0) {
                    candidates[candidate_count++] = {q.venue, q.bid_price, q.bid_qty, q.fee_bps};
                }
            }
        }

        if (candidate_count == 0) return slices;

        // Sort candidates:
        // Buy: lowest price first, then lowest fee
        // Sell: highest price first, then lowest fee
        std::sort(candidates.begin(), candidates.begin() + candidate_count,
                  [side](const RankedVenue& a, const RankedVenue& b) {
                      if (a.available_price != b.available_price) {
                          return (side == exec::kBuy)
                              ? (a.available_price < b.available_price)
                              : (a.available_price > b.available_price);
                      }
                      return a.fee_bps < b.fee_bps;
                  });

        int64_t remaining_qty = target_qty;
        for (uint8_t i = 0; i < candidate_count && remaining_qty > 0; ++i) {
            const auto& c = candidates[i];
            const int64_t slice_qty = std::min(remaining_qty, c.available_qty);
            if (slice_qty > 0) {
                VenueRouteSlice slice{};
                slice.venue = c.venue;
                slice.price = c.available_price;
                slice.qty = slice_qty;
                slice.client_order_id = base_client_id + static_cast<uint32_t>(slices.size());
                slices.push_back(slice);
                remaining_qty -= slice_qty;
            }
        }

        // If remaining quantity remains unsatisfied by displayed liquidity, route remainder to primary venue
        if (remaining_qty > 0) {
            VenueRouteSlice remainder_slice{};
            remainder_slice.venue = VenueId::kNasdaq;
            remainder_slice.price = limit_price;
            remainder_slice.qty = remaining_qty;
            remainder_slice.client_order_id = base_client_id + static_cast<uint32_t>(slices.size());
            slices.push_back(remainder_slice);
        }

        return slices;
    }

private:
    std::array<VenueQuote, kMaxVenues> _quotes{};
};

} // namespace luv
