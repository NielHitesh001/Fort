#pragma once

#include <cstdint>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {

struct MicrostructureFeatures {
    double micro_price{0.0};       // Volume-weighted mid price
    double ofi{0.0};               // Order Flow Imbalance (Cont et al.)
    double spread_bps{0.0};        // Relative spread in basis points
    double book_imbalance{0.0};    // (BidQty - AskQty) / (BidQty + AskQty)
    double trade_imbalance{0.0};   // (BuyVol - SellVol) / TotalVol
};

class MicrostructureFeatureEngine {
public:
    static constexpr size_t kRingCapacity = 64;

    MicrostructureFeatureEngine() noexcept {
        prev_bid_price_ = 0;
        prev_bid_size_ = 0;
        prev_ask_price_ = 0;
        prev_ask_size_ = 0;
    }

    // Update with top of book
    void on_quote(uint64_t bid_price, uint64_t bid_size, uint64_t ask_price, uint64_t ask_size) noexcept {
        if (bid_price == 0 || ask_price == 0 || bid_size + ask_size == 0) return;

        double bp = static_cast<double>(bid_price);
        double bsz = static_cast<double>(bid_size);
        double ap = static_cast<double>(ask_price);
        double asz = static_cast<double>(ask_size);

        // 1. Micro-price: (BidPrice * AskQty + AskPrice * BidQty) / (BidQty + AskQty)
        features_.micro_price = (bp * asz + ap * bsz) / (bsz + asz);

        // 2. Book Imbalance: (BidQty - AskQty) / (BidQty + AskQty)
        features_.book_imbalance = (bsz - asz) / (bsz + asz);

        // 3. Spread in BPS: ((Ask - Bid) / Mid) * 10,000
        double mid = 0.5 * (bp + ap);
        if (mid > 0) {
            features_.spread_bps = ((ap - bp) / mid) * 10000.0;
        }

        // 4. Order Flow Imbalance (OFI)
        if (prev_bid_price_ > 0 && prev_ask_price_ > 0) {
            double delta_bid = 0.0;
            if (bid_price > prev_bid_price_) delta_bid = bsz;
            else if (bid_price == prev_bid_price_) delta_bid = bsz - static_cast<double>(prev_bid_size_);
            else delta_bid = -static_cast<double>(prev_bid_size_);

            double delta_ask = 0.0;
            if (ask_price < prev_ask_price_) delta_ask = asz;
            else if (ask_price == prev_ask_price_) delta_ask = asz - static_cast<double>(prev_ask_size_);
            else delta_ask = -static_cast<double>(prev_ask_size_);

            features_.ofi = delta_bid - delta_ask;
        }

        prev_bid_price_ = bid_price;
        prev_bid_size_ = bid_size;
        prev_ask_price_ = ask_price;
        prev_ask_size_ = ask_size;
    }

    // Update with executed trades
    void on_trade(bool is_buyer_initiated, uint64_t quantity) noexcept {
        if (is_buyer_initiated) {
            cumulative_buy_vol_ += quantity;
        } else {
            cumulative_sell_vol_ += quantity;
        }

        uint64_t total = cumulative_buy_vol_ + cumulative_sell_vol_;
        if (total > 0) {
            features_.trade_imbalance = static_cast<double>(static_cast<int64_t>(cumulative_buy_vol_) - static_cast<int64_t>(cumulative_sell_vol_)) / static_cast<double>(total);
        }
    }

    const MicrostructureFeatures& get_features() const noexcept { return features_; }

private:
    uint64_t prev_bid_price_{0};
    uint64_t prev_bid_size_{0};
    uint64_t prev_ask_price_{0};
    uint64_t prev_ask_size_{0};

    uint64_t cumulative_buy_vol_{0};
    uint64_t cumulative_sell_vol_{0};

    MicrostructureFeatures features_{};
};

} // namespace luv
