#pragma once

#include <cstdint>
#include <array>
#include <algorithm>

namespace luv {
namespace clearing {

enum class DtcMatchStatus : uint8_t {
    kAffirmedMatched = 0,
    kQuantityMismatch = 1,
    kSettlementPriceMismatch = 2,
    kUnmatchedCounterparty = 3,
    kFailedToDeliver = 4
};

struct DtcDeliveryInstruction {
    uint64_t instruction_id = 0;
    uint32_t delivering_participant_id = 0;
    uint32_t receiving_participant_id = 0;
    uint16_t cusip_symbol_idx = 0;
    int64_t share_qty = 0;
    int64_t settlement_amount_cents = 0;
    uint64_t settlement_date_ns = 0;
    bool affirmed = false;
};

struct DtcSettlementMatchResult {
    uint64_t instruction_id = 0;
    DtcMatchStatus status = DtcMatchStatus::kUnmatchedCounterparty;
    bool buy_in_notice_required = false;
};

class DtcSettlementEngine {
public:
    static constexpr size_t kMaxInstructions = 256;

    DtcSettlementEngine() noexcept : num_deliveries_(0), num_receives_(0) {}

    bool record_deliver_instruction(const DtcDeliveryInstruction& inst) noexcept {
        if (num_deliveries_ >= kMaxInstructions) return false;
        deliveries_[num_deliveries_++] = inst;
        return true;
    }

    bool record_receive_instruction(const DtcDeliveryInstruction& inst) noexcept {
        if (num_receives_ >= kMaxInstructions) return false;
        receives_[num_receives_++] = inst;
        return true;
    }

    // Matches delivery vs receive instructions and identifies FTD fails
    size_t match_settlements(uint64_t current_time_ns, DtcSettlementMatchResult* out_results, size_t max_out) noexcept {
        if (!out_results || max_out == 0) return 0;

        size_t count = 0;

        for (size_t i = 0; i < num_deliveries_ && count < max_out; ++i) {
            const auto& del = deliveries_[i];
            const DtcDeliveryInstruction* rec_match = nullptr;

            for (size_t j = 0; j < num_receives_; ++j) {
                if (receives_[j].delivering_participant_id == del.delivering_participant_id &&
                    receives_[j].receiving_participant_id == del.receiving_participant_id &&
                    receives_[j].cusip_symbol_idx == del.cusip_symbol_idx) {
                    rec_match = &receives_[j];
                    break;
                }
            }

            DtcSettlementMatchResult res{};
            res.instruction_id = del.instruction_id;

            if (!rec_match) {
                res.status = (current_time_ns > del.settlement_date_ns) 
                    ? DtcMatchStatus::kFailedToDeliver 
                    : DtcMatchStatus::kUnmatchedCounterparty;
                res.buy_in_notice_required = (res.status == DtcMatchStatus::kFailedToDeliver);
            } else if (del.share_qty != rec_match->share_qty) {
                res.status = DtcMatchStatus::kQuantityMismatch;
            } else if (del.settlement_amount_cents != rec_match->settlement_amount_cents) {
                res.status = DtcMatchStatus::kSettlementPriceMismatch;
            } else {
                res.status = DtcMatchStatus::kAffirmedMatched;
            }

            out_results[count++] = res;
        }

        return count;
    }

private:
    std::array<DtcDeliveryInstruction, kMaxInstructions> deliveries_{};
    size_t num_deliveries_{0};
    std::array<DtcDeliveryInstruction, kMaxInstructions> receives_{};
    size_t num_receives_{0};
};

} // namespace clearing
} // namespace luv
