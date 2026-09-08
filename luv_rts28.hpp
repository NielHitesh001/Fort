#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <algorithm>
#include <string_view>

namespace luv {

// MiFID II Asset Classifications according to RTS 28 Annex I
enum class InstrumentClass : uint8_t {
    Equities_SharesAndDepositaryReceipts = 0,
    DebtInstruments_Bonds = 1,
    DebtInstruments_MoneyMarketInstruments = 2,
    InterestRateDerivatives = 3,
    CreditDerivatives = 4,
    CurrencyDerivatives = 5,
    StructuredFinanceInstruments = 6,
    EquityDerivatives = 7,
    CommodityDerivatives = 8,
    SecuritizedDerivatives = 9,
    ContractsForDifference = 10,
    ExchangeTradedProducts = 11,
    EmissionAllowances = 12,
    OtherInstruments = 13,
    COUNT = 14
};

enum class ClientCategory : uint8_t {
    Professional = 0,
    Retail = 1
};

enum class OrderFlowType : uint8_t {
    Passive = 0,      // Provided liquidity (maker)
    Aggressive = 1,   // Took liquidity (taker)
    Directed = 2      // Client directed execution venue
};

struct ExecutionRecord {
    uint64_t order_id{0};
    InstrumentClass inst_class{InstrumentClass::Equities_SharesAndDepositaryReceipts};
    ClientCategory client_cat{ClientCategory::Professional};
    char venue_mic[8]{0};       // Market Identifier Code (e.g. "XNAS", "XNYS", "BATS", "XCBO")
    char venue_lei[24]{0};      // Legal Entity Identifier (20 alphanumeric chars)
    uint64_t quantity{0};
    double price{0.0};
    double gross_notional{0.0}; // price * quantity
    bool is_passive{false};
    bool is_aggressive{false};
    bool is_directed{false};
};

struct TopVenueReportEntry {
    char venue_mic[8]{0};
    char venue_lei[24]{0};
    double volume_percentage{0.0};     // % of total volume in class (0.0 - 100.0)
    double order_percentage{0.0};      // % of total orders in class (0.0 - 100.0)
    double passive_percentage{0.0};    // % passive orders executed at this venue
    double aggressive_percentage{0.0}; // % aggressive orders executed at this venue
    double directed_percentage{0.0};   // % directed orders executed at this venue
    double total_notional{0.0};
    uint64_t total_orders{0};
};

struct RTS28ClassSummary {
    InstrumentClass inst_class{InstrumentClass::Equities_SharesAndDepositaryReceipts};
    ClientCategory client_cat{ClientCategory::Professional};
    double total_class_notional{0.0};
    uint64_t total_class_orders{0};
    size_t venue_count{0};
    std::array<TopVenueReportEntry, 5> top_venues{};
};

class RTS28Reporter {
public:
    static constexpr size_t MAX_VENUES_PER_CLASS = 32;

    struct VenueAccumulator {
        char venue_mic[8]{0};
        char venue_lei[24]{0};
        double notional{0.0};
        uint64_t order_count{0};
        uint64_t passive_count{0};
        uint64_t aggressive_count{0};
        uint64_t directed_count{0};
        bool active{false};
    };

    struct ClassAccumulator {
        double total_notional{0.0};
        uint64_t total_orders{0};
        size_t active_venue_count{0};
        std::array<VenueAccumulator, MAX_VENUES_PER_CLASS> venues{};
    };

    RTS28Reporter() noexcept {
        reset();
    }

    void reset() noexcept {
        for (size_t c = 0; c < 2; ++c) {
            for (size_t i = 0; i < static_cast<size_t>(InstrumentClass::COUNT); ++i) {
                accumulators_[c][i] = ClassAccumulator{};
            }
        }
        total_records_processed_ = 0;
    }

    inline bool record_execution(const ExecutionRecord& exec) noexcept {
        size_t c_idx = static_cast<size_t>(exec.client_cat);
        size_t i_idx = static_cast<size_t>(exec.inst_class);
        if (c_idx >= 2 || i_idx >= static_cast<size_t>(InstrumentClass::COUNT)) {
            return false;
        }

        auto& cat_acc = accumulators_[c_idx][i_idx];
        double notional = exec.gross_notional > 0.0 ? exec.gross_notional : (exec.price * static_cast<double>(exec.quantity));

        cat_acc.total_notional += notional;
        cat_acc.total_orders += 1;

        // Find or create venue entry
        VenueAccumulator* target = nullptr;
        for (size_t v = 0; v < cat_acc.active_venue_count; ++v) {
            if (std::strncmp(cat_acc.venues[v].venue_mic, exec.venue_mic, sizeof(exec.venue_mic)) == 0) {
                target = &cat_acc.venues[v];
                break;
            }
        }

        if (!target) {
            if (cat_acc.active_venue_count >= MAX_VENUES_PER_CLASS) {
                return false; // Venue capacity reached for class
            }
            target = &cat_acc.venues[cat_acc.active_venue_count++];
            std::strncpy(target->venue_mic, exec.venue_mic, sizeof(target->venue_mic) - 1);
            target->venue_mic[sizeof(target->venue_mic) - 1] = '\0';
            std::strncpy(target->venue_lei, exec.venue_lei, sizeof(target->venue_lei) - 1);
            target->venue_lei[sizeof(target->venue_lei) - 1] = '\0';
            target->active = true;
        }

        target->notional += notional;
        target->order_count += 1;
        if (exec.is_passive) target->passive_count += 1;
        if (exec.is_aggressive) target->aggressive_count += 1;
        if (exec.is_directed) target->directed_count += 1;

        ++total_records_processed_;
        return true;
    }

    RTS28ClassSummary generate_report(InstrumentClass inst_class, ClientCategory client_cat) const noexcept {
        RTS28ClassSummary summary{};
        summary.inst_class = inst_class;
        summary.client_cat = client_cat;

        size_t c_idx = static_cast<size_t>(client_cat);
        size_t i_idx = static_cast<size_t>(inst_class);
        if (c_idx >= 2 || i_idx >= static_cast<size_t>(InstrumentClass::COUNT)) {
            return summary;
        }

        const auto& cat_acc = accumulators_[c_idx][i_idx];
        summary.total_class_notional = cat_acc.total_notional;
        summary.total_class_orders = cat_acc.total_orders;

        if (cat_acc.active_venue_count == 0 || cat_acc.total_orders == 0) {
            return summary;
        }

        // Copy active venues to local array for sorting
        std::array<VenueAccumulator, MAX_VENUES_PER_CLASS> sorted_venues = cat_acc.venues;
        size_t count = cat_acc.active_venue_count;

        std::sort(sorted_venues.begin(), sorted_venues.begin() + count, [](const VenueAccumulator& a, const VenueAccumulator& b) {
            return a.notional > b.notional; // Descending by traded volume
        });

        size_t top_n = std::min(count, static_cast<size_t>(5));
        summary.venue_count = top_n;

        for (size_t i = 0; i < top_n; ++i) {
            const auto& v = sorted_venues[i];
            auto& out = summary.top_venues[i];
            std::strncpy(out.venue_mic, v.venue_mic, sizeof(out.venue_mic) - 1);
            out.venue_mic[sizeof(out.venue_mic) - 1] = '\0';
            std::strncpy(out.venue_lei, v.venue_lei, sizeof(out.venue_lei) - 1);
            out.venue_lei[sizeof(out.venue_lei) - 1] = '\0';

            out.total_notional = v.notional;
            out.total_orders = v.order_count;

            out.volume_percentage = (cat_acc.total_notional > 0.0) 
                ? (v.notional / cat_acc.total_notional) * 100.0 : 0.0;
            out.order_percentage = (cat_acc.total_orders > 0) 
                ? (static_cast<double>(v.order_count) / static_cast<double>(cat_acc.total_orders)) * 100.0 : 0.0;
            out.passive_percentage = (v.order_count > 0) 
                ? (static_cast<double>(v.passive_count) / static_cast<double>(v.order_count)) * 100.0 : 0.0;
            out.aggressive_percentage = (v.order_count > 0) 
                ? (static_cast<double>(v.aggressive_count) / static_cast<double>(v.order_count)) * 100.0 : 0.0;
            out.directed_percentage = (v.order_count > 0) 
                ? (static_cast<double>(v.directed_count) / static_cast<double>(v.order_count)) * 100.0 : 0.0;
        }

        return summary;
    }

    uint64_t total_records_processed() const noexcept {
        return total_records_processed_;
    }

private:
    std::array<std::array<ClassAccumulator, static_cast<size_t>(InstrumentClass::COUNT)>, 2> accumulators_{};
    uint64_t total_records_processed_{0};
};

} // namespace luv
