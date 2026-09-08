#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

enum class FtdType : uint8_t {
    ShortSale = 0,       // Requires close-out by S+1 market open
    LongSale = 1,        // Requires close-out by S+3 market open
    Rule144Restricted = 2 // Requires close-out by S+5 market open
};

enum class CloseOutStatus : uint8_t {
    Open = 0,
    Cured = 1,
    MandatoryBuyInTriggered = 2,
    PenaltyBoxRestriction = 3
};

struct FtdRecord {
    uint64_t ftd_id{0};
    uint64_t participant_id{0};
    uint64_t symbol_id{0};
    uint64_t fail_quantity{0};
    uint64_t cured_quantity{0};
    uint32_t settlement_day{0};    // Day of settlement failure (S)
    uint32_t current_day{0};       // Current business day
    FtdType type{FtdType::ShortSale};
    CloseOutStatus status{CloseOutStatus::Open};
};

class RegSho204Engine {
public:
    static constexpr size_t kMaxRecords = 256;
    static constexpr size_t kMaxPenaltySymbols = 64;

    RegSho204Engine() noexcept : record_count_(0), penalty_count_(0) {}

    bool register_ftd(
        uint64_t ftd_id,
        uint64_t participant_id,
        uint64_t symbol_id,
        uint64_t fail_qty,
        uint32_t settlement_day,
        FtdType type) noexcept
    {
        if (record_count_ >= kMaxRecords) return false;

        auto& r = records_[record_count_++];
        r.ftd_id = ftd_id;
        r.participant_id = participant_id;
        r.symbol_id = symbol_id;
        r.fail_quantity = fail_qty;
        r.cured_quantity = 0;
        r.settlement_day = settlement_day;
        r.current_day = settlement_day;
        r.type = type;
        r.status = CloseOutStatus::Open;

        return true;
    }

    // Process cure (deliveries / borrows borrowed to resolve FTD)
    bool cure_ftd(uint64_t ftd_id, uint64_t delivered_qty) noexcept {
        for (size_t i = 0; i < record_count_; ++i) {
            if (records_[i].ftd_id == ftd_id && records_[i].status == CloseOutStatus::Open) {
                records_[i].cured_quantity += delivered_qty;
                if (records_[i].cured_quantity >= records_[i].fail_quantity) {
                    records_[i].status = CloseOutStatus::Cured;
                }
                return true;
            }
        }
        return false;
    }

    // Advance business day and check close-out requirements under SEC Rule 204
    void advance_business_day(uint32_t new_day) noexcept {
        for (size_t i = 0; i < record_count_; ++i) {
            auto& r = records_[i];
            if (r.status != CloseOutStatus::Open) continue;

            r.current_day = new_day;
            uint32_t days_elapsed = (new_day >= r.settlement_day) ? (new_day - r.settlement_day) : 0;

            uint32_t max_days = 1; // S+1 for short sales
            if (r.type == FtdType::LongSale) max_days = 3; // S+3 for long sales
            else if (r.type == FtdType::Rule144Restricted) max_days = 5; // S+5 for Rule 144

            if (days_elapsed >= max_days) {
                // Trigger mandatory open-market buy-in
                r.status = CloseOutStatus::MandatoryBuyInTriggered;
                // Add symbol to participant penalty box (pre-borrow required for all future short sales)
                add_to_penalty_box(r.symbol_id);
            }
        }
    }

    bool is_pre_borrow_required(uint64_t symbol_id) const noexcept {
        for (size_t i = 0; i < penalty_count_; ++i) {
            if (penalty_symbols_[i] == symbol_id) return true;
        }
        return false;
    }

    const FtdRecord* get_record(uint64_t ftd_id) const noexcept {
        for (size_t i = 0; i < record_count_; ++i) {
            if (records_[i].ftd_id == ftd_id) return &records_[i];
        }
        return nullptr;
    }

    size_t get_open_ftd_count() const noexcept {
        size_t count = 0;
        for (size_t i = 0; i < record_count_; ++i) {
            if (records_[i].status == CloseOutStatus::Open) ++count;
        }
        return count;
    }

private:
    void add_to_penalty_box(uint64_t symbol_id) noexcept {
        if (is_pre_borrow_required(symbol_id)) return;
        if (penalty_count_ < kMaxPenaltySymbols) {
            penalty_symbols_[penalty_count_++] = symbol_id;
        }
    }

    std::array<FtdRecord, kMaxRecords> records_{};
    size_t record_count_{0};

    std::array<uint64_t, kMaxPenaltySymbols> penalty_symbols_{};
    size_t penalty_count_{0};
};

} // namespace luv
