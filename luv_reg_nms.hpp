#pragma once

#include <cstdint>
#include <array>
#include "luv_execution.hpp"

namespace luv {
namespace reg_nms {

// Protected Venue NBBO Quote
struct ProtectedBBO {
    uint8_t venue_id = 0;
    int64_t best_bid = 0;
    int64_t best_ask = 0;
    int64_t bid_size = 0;
    int64_t ask_size = 0;
    uint64_t quote_ts_ns = 0;
};

// National Best Bid and Offer (NBBO)
struct NbboState {
    int64_t national_best_bid = 0;
    int64_t national_best_ask = 0;
    uint8_t best_bid_venue = 0;
    uint8_t best_ask_venue = 0;
    int64_t total_bid_size = 0;
    int64_t total_ask_size = 0;
};

class RegNmsRule611Validator {
public:
    static constexpr size_t kMaxProtectedVenues = 16;

    RegNmsRule611Validator() noexcept : num_venues_(0) {}

    void update_venue_bbo(uint8_t venue_id, int64_t bid, int64_t ask, int64_t bid_sz, int64_t ask_sz, uint64_t ts) noexcept {
        for (size_t i = 0; i < num_venues_; ++i) {
            if (venues_[i].venue_id == venue_id) {
                venues_[i] = ProtectedBBO{venue_id, bid, ask, bid_sz, ask_sz, ts};
                return;
            }
        }
        if (num_venues_ < kMaxProtectedVenues) {
            venues_[num_venues_++] = ProtectedBBO{venue_id, bid, ask, bid_sz, ask_sz, ts};
        }
    }

    // Computes NBBO across all active protected venues
    NbboState compute_nbbo() const noexcept {
        NbboState nbbo;
        nbbo.national_best_bid = 0;
        nbbo.national_best_ask = INT64_MAX;

        for (size_t i = 0; i < num_venues_; ++i) {
            if (venues_[i].best_bid > nbbo.national_best_bid) {
                nbbo.national_best_bid = venues_[i].best_bid;
                nbbo.best_bid_venue = venues_[i].venue_id;
            }
            if (venues_[i].best_ask < nbbo.national_best_ask && venues_[i].best_ask > 0) {
                nbbo.national_best_ask = venues_[i].best_ask;
                nbbo.best_ask_venue = venues_[i].venue_id;
            }
        }

        // Aggregate depth at NBBO
        for (size_t i = 0; i < num_venues_; ++i) {
            if (venues_[i].best_bid == nbbo.national_best_bid) {
                nbbo.total_bid_size += venues_[i].bid_size;
            }
            if (venues_[i].best_ask == nbbo.national_best_ask) {
                nbbo.total_ask_size += venues_[i].ask_size;
            }
        }

        return nbbo;
    }

    // Evaluates if an execution at `local_exec_price` would constitute an unlawful Trade-Through
    // Returns true if compliant, false if trade-through violation occurs
    bool validate_execution(
        uint8_t side,
        int64_t local_exec_price,
        bool is_iso) const noexcept
    {
        // ISO (Intermarket Sweep Order) orders bypass local Rule 611 check because the router sweeps external venues concurrently
        if (is_iso) return true;

        NbboState nbbo = compute_nbbo();

        if (side == exec::kBuy) {
            // Buyer execution price cannot be worse (higher) than the National Best Ask
            if (nbbo.national_best_ask < INT64_MAX && local_exec_price > nbbo.national_best_ask) {
                return false; // Trade-through violation on Ask
            }
        } else {
            // Seller execution price cannot be worse (lower) than the National Best Bid
            if (nbbo.national_best_bid > 0 && local_exec_price < nbbo.national_best_bid) {
                return false; // Trade-through violation on Bid
            }
        }

        return true;
    }

    void reset() noexcept { num_venues_ = 0; }

private:
    std::array<ProtectedBBO, kMaxProtectedVenues> venues_{};
    size_t num_venues_{0};
};

} // namespace reg_nms
} // namespace luv
