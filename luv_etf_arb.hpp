#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace arb {

struct BasketConstituent {
    uint16_t symbol_idx = 0;
    int64_t share_count = 0; // Number of shares per ETF creation unit
    int64_t current_price = 0; // Scaled x10,000
};

enum class ArbSignalType : uint8_t {
    kNeutral = 0,
    kCreateEtfArb = 1, // ETF trading at premium: Buy basket, create ETF, sell ETF
    kRedeemEtfArb = 2  // ETF trading at discount: Buy ETF, redeem basket, sell basket
};

struct EtfArbOpportunity {
    ArbSignalType signal = ArbSignalType::kNeutral;
    int64_t inav_price = 0;           // Scaled x10,000
    int64_t etf_market_price = 0;     // Scaled x10,000
    int64_t net_mispricing_bps = 0;  // Basis points discrepancy after creation fees
};

class EtfArbitrageEngine {
public:
    static constexpr size_t kMaxConstituents = 64;
    static constexpr int64_t kCreationUnitShares = 50'000; // 50,000 ETF shares per Creation Unit
    static constexpr int64_t kCreationFeeNotional = 500'0000; // $500 fixed create/redeem fee (scaled)

    EtfArbitrageEngine() noexcept : num_constituents_(0), cash_component_(0) {}

    bool add_constituent(uint16_t sym, int64_t share_count, int64_t price) noexcept {
        if (num_constituents_ >= kMaxConstituents || share_count <= 0) return false;
        constituents_[num_constituents_++] = BasketConstituent{sym, share_count, price};
        return true;
    }

    void set_cash_component(int64_t cash) noexcept { cash_component_ = cash; }

    // Computes intraday Indicative Value (iNAV) per ETF share
    int64_t compute_inav() const noexcept {
        if (num_constituents_ == 0) return 0;

        int64_t total_basket_val = cash_component_;
        for (size_t i = 0; i < num_constituents_; ++i) {
            total_basket_val += (constituents_[i].share_count * constituents_[i].current_price);
        }

        // iNAV = Total Basket Value / CreationUnitShares
        return total_basket_val / kCreationUnitShares;
    }

    // Evaluates ETF market price vs iNAV for arbitrage profitability (> 10 bps threshold)
    EtfArbOpportunity evaluate_arbitrage(int64_t etf_bid, int64_t etf_ask) const noexcept {
        EtfArbOpportunity opp{};
        int64_t inav = compute_inav();
        opp.inav_price = inav;

        if (inav <= 0) return opp;

        // 1. Creation Arb Check: ETF Bid > iNAV + CreationFee
        int64_t fee_per_share = kCreationFeeNotional / kCreationUnitShares;
        if (etf_bid > inav + fee_per_share) {
            int64_t premium = etf_bid - (inav + fee_per_share);
            int64_t bps = (premium * 10000) / inav;
            if (bps >= 5) { // >= 5 bps threshold
                opp.signal = ArbSignalType::kCreateEtfArb;
                opp.etf_market_price = etf_bid;
                opp.net_mispricing_bps = bps;
                return opp;
            }
        }

        // 2. Redemption Arb Check: ETF Ask < iNAV - CreationFee
        if (etf_ask + fee_per_share < inav) {
            int64_t discount = inav - (etf_ask + fee_per_share);
            int64_t bps = (discount * 10000) / inav;
            if (bps >= 5) { // >= 5 bps threshold
                opp.signal = ArbSignalType::kRedeemEtfArb;
                opp.etf_market_price = etf_ask;
                opp.net_mispricing_bps = bps;
                return opp;
            }
        }

        return opp;
    }

private:
    std::array<BasketConstituent, kMaxConstituents> constituents_{};
    size_t num_constituents_{0};
    int64_t cash_component_{0};
};

} // namespace arb
} // namespace luv
