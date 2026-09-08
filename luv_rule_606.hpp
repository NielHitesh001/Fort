#pragma once

#include <cstdint>
#include <array>
#include <algorithm>

namespace luv {
namespace compliance {

struct VenueRoutingStat {
    uint8_t venue_id = 0;
    int64_t non_directed_orders = 0;
    int64_t market_orders = 0;
    int64_t marketable_limit_orders = 0;
    int64_t non_marketable_limit_orders = 0;
    int64_t net_pfof_received_cents = 0; // Payment for order flow (positive = received, negative = paid)
};

struct Rule606QuarterlySummary {
    int64_t total_non_directed_orders = 0;
    double top_venue_pct = 0.0;
    uint8_t top_venue_id = 0;
    int64_t total_net_pfof_cents = 0;
};

class Rule606Reporter {
public:
    static constexpr size_t kMaxVenues = 16;

    Rule606Reporter() noexcept : num_venues_(0) {}

    bool record_venue_order(uint8_t venue_id, bool is_market, bool is_marketable_limit, int64_t pfof_cents) noexcept {
        VenueRoutingStat* v = nullptr;
        for (size_t i = 0; i < num_venues_; ++i) {
            if (venues_[i].venue_id == venue_id) {
                v = &venues_[i];
                break;
            }
        }

        if (!v) {
            if (num_venues_ >= kMaxVenues) return false;
            v = &venues_[num_venues_++];
            v->venue_id = venue_id;
        }

        v->non_directed_orders++;
        if (is_market) v->market_orders++;
        else if (is_marketable_limit) v->marketable_limit_orders++;
        else v->non_marketable_limit_orders++;

        v->net_pfof_received_cents += pfof_cents;
        return true;
    }

    Rule606QuarterlySummary generate_summary() const noexcept {
        Rule606QuarterlySummary summary{};
        if (num_venues_ == 0) return summary;

        int64_t total_orders = 0;
        int64_t max_venue_orders = 0;

        for (size_t i = 0; i < num_venues_; ++i) {
            total_orders += venues_[i].non_directed_orders;
            summary.total_net_pfof_cents += venues_[i].net_pfof_received_cents;

            if (venues_[i].non_directed_orders > max_venue_orders) {
                max_venue_orders = venues_[i].non_directed_orders;
                summary.top_venue_id = venues_[i].venue_id;
            }
        }

        summary.total_non_directed_orders = total_orders;
        if (total_orders > 0) {
            summary.top_venue_pct = (static_cast<double>(max_venue_orders) / static_cast<double>(total_orders)) * 100.0;
        }

        return summary;
    }

private:
    std::array<VenueRoutingStat, kMaxVenues> venues_{};
    size_t num_venues_{0};
};

} // namespace compliance
} // namespace luv
