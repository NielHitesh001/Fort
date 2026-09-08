#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <algorithm>
#include <string_view>
#include <cstdio>

namespace luv {

// CFTC Asset Classes under 17 CFR Part 43 / 45
enum class CFTCSwapAssetClass : uint8_t {
    InterestRate = 0,
    Credit = 1,
    ForeignExchange = 2,
    Equity = 3,
    Commodity = 4,
    COUNT = 5
};

// Swap Execution Facility (SEF) / DCM / Off-Facility classification
enum class CFTCExecutionVenueType : uint8_t {
    SEF = 0,               // Swap Execution Facility
    DCM = 1,               // Designated Contract Market
    OffFacility_Bilateral = 2
};

enum class CFTCReportingState : uint8_t {
    Part43_PublicRealtime = 0,   // Public dissemination
    Part45_SDRCreation = 1,       // SDR primary economic terms (PET)
    Part45_SDRContinuation = 2,   // Lifecycle valuation / confirmation
    COUNT = 3
};

struct SwapTradeRecord {
    uint64_t trade_id{0};
    CFTCSwapAssetClass asset_class{CFTCSwapAssetClass::InterestRate};
    CFTCExecutionVenueType venue_type{CFTCExecutionVenueType::SEF};
    char reporting_counterparty_lei[24]{0}; // 20-char LEI of reporting SDR entity
    char non_reporting_counterparty_lei[24]{0};
    char usi_uti[52]{0};                    // 42-char CFTC USI/UTI identifier
    double notional_amount{0.0};
    char notional_currency[4]{0};           // e.g. "USD", "EUR"
    double fixed_rate_or_price{0.0};
    double floating_spread_bp{0.0};
    uint64_t execution_timestamp_ns{0};
    bool is_block_trade{false};             // Meets CFTC block trade size threshold
    bool is_cleared{true};
};

struct CFTCDisseminationRecord {
    char usi_uti[52]{0};
    CFTCSwapAssetClass asset_class{CFTCSwapAssetClass::InterestRate};
    double capped_rounded_notional{0.0};   // Part 43 post-trade rounding/capping
    double execution_price{0.0};
    uint64_t public_release_time_ns{0};
    bool is_block_trade{false};
    bool reported_to_sdr{false};
};

class CFTCSwapReportingEngine {
public:
    static constexpr size_t MAX_RECORDS = 256;
    static constexpr uint64_t REALTIME_DELAY_NS = 0; // Immediate for non-block SEF trades
    static constexpr uint64_t BLOCK_TRADE_DELAY_NS = 15ULL * 60 * 1'000'000'000ULL; // 15 min delay for block trades

    CFTCSwapReportingEngine() noexcept {
        reset();
    }

    void reset() noexcept {
        trade_count_ = 0;
        disseminated_count_ = 0;
    }

    // Generates compliant CFTC USI / UTI: 20-char LEI prefix + 22-char zero-padded monotonic sequence
    static bool generate_usi_uti(const char* reporting_lei, uint64_t trade_seq, char* out_buf, size_t buf_len) noexcept {
        if (!reporting_lei || !out_buf || buf_len < 43) return false;
        // Format: [20 LEI][22 digit seq]
        std::snprintf(out_buf, buf_len, "%-20.20s%022llu", reporting_lei, static_cast<unsigned long long>(trade_seq));
        return true;
    }

    // Part 43 Rounding and Capping rules for public transparency
    static double compute_part43_public_notional(CFTCSwapAssetClass asset_class, double actual_notional, bool is_block) noexcept {
        (void)is_block;
        // Capping thresholds per CFTC Part 43 Appendix F (e.g. USD 250M for IR, USD 100M for Credit)
        double cap_threshold = 250'000'000.0;
        switch (asset_class) {
            case CFTCSwapAssetClass::InterestRate: cap_threshold = 250'000'000.0; break;
            case CFTCSwapAssetClass::Credit:       cap_threshold = 100'000'000.0; break;
            case CFTCSwapAssetClass::ForeignExchange: cap_threshold = 250'000'000.0; break;
            case CFTCSwapAssetClass::Equity:       cap_threshold = 100'000'000.0; break;
            case CFTCSwapAssetClass::Commodity:    cap_threshold = 50'000'000.0; break;
            default: cap_threshold = 100'000'000.0; break;
        }

        double capped = std::min(actual_notional, cap_threshold);
        // Rounding: round to nearest $1M if > $100M, to nearest $100k if between $10M and $100M, else $10k
        if (capped >= 100'000'000.0) {
            return std::round(capped / 1'000'000.0) * 1'000'000.0;
        } else if (capped >= 10'000'000.0) {
            return std::round(capped / 100'000.0) * 100'000.0;
        } else if (capped >= 1'000'000.0) {
            return std::round(capped / 10'000.0) * 10'000.0;
        }
        return capped;
    }

    bool submit_swap_trade(const SwapTradeRecord& trade) noexcept {
        if (trade_count_ >= MAX_RECORDS) return false;
        trades_[trade_count_++] = trade;

        // Process Part 43 Public Dissemination
        CFTCDisseminationRecord diss{};
        std::strncpy(diss.usi_uti, trade.usi_uti, sizeof(diss.usi_uti) - 1);
        diss.asset_class = trade.asset_class;
        diss.capped_rounded_notional = compute_part43_public_notional(trade.asset_class, trade.notional_amount, trade.is_block_trade);
        diss.execution_price = trade.fixed_rate_or_price;
        diss.is_block_trade = trade.is_block_trade;
        diss.public_release_time_ns = trade.execution_timestamp_ns + (trade.is_block_trade ? BLOCK_TRADE_DELAY_NS : REALTIME_DELAY_NS);
        diss.reported_to_sdr = true;

        disseminated_[disseminated_count_++] = diss;
        return true;
    }

    size_t trade_count() const noexcept { return trade_count_; }
    size_t disseminated_count() const noexcept { return disseminated_count_; }

    const CFTCDisseminationRecord* get_dissemination_record(size_t index) const noexcept {
        if (index >= disseminated_count_) return nullptr;
        return &disseminated_[index];
    }

private:
    std::array<SwapTradeRecord, MAX_RECORDS> trades_{};
    size_t trade_count_{0};

    std::array<CFTCDisseminationRecord, MAX_RECORDS> disseminated_{};
    size_t disseminated_count_{0};
};

} // namespace luv
