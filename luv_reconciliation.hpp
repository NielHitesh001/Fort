#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace recon {

enum class BreakType : uint8_t {
    kMatched = 0,
    kQuantityBreak = 1,
    kPriceBreak = 2,
    kSideBreak = 3,
    kUnmatchedFrontOffice = 4,
    kUnmatchedBackOffice = 5
};

struct ExecutionEntry {
    uint64_t order_id = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    int64_t price = 0;
    int64_t qty = 0;
    bool matched = false;
};

struct ReconBreakReport {
    uint64_t order_id = 0;
    BreakType break_type = BreakType::kMatched;
    int64_t fo_qty = 0;
    int64_t bo_qty = 0;
    int64_t fo_price = 0;
    int64_t bo_price = 0;
};

class TradeReconciliationEngine {
public:
    static constexpr size_t kMaxRecords = 512;

    TradeReconciliationEngine() noexcept : num_fo_(0), num_bo_(0) {}

    bool record_front_office(uint64_t order_id, uint16_t sym, uint8_t side, int64_t price, int64_t qty) noexcept {
        if (num_fo_ >= kMaxRecords) return false;
        fo_trades_[num_fo_++] = ExecutionEntry{order_id, sym, side, price, qty, false};
        return true;
    }

    bool record_back_office(uint64_t order_id, uint16_t sym, uint8_t side, int64_t price, int64_t qty) noexcept {
        if (num_bo_ >= kMaxRecords) return false;
        bo_trades_[num_bo_++] = ExecutionEntry{order_id, sym, side, price, qty, false};
        return true;
    }

    // Runs reconciliation matching and writes break reports to buffer
    size_t reconcile_trades(ReconBreakReport* out_breaks, size_t max_breaks) noexcept {
        if (!out_breaks || max_breaks == 0) return 0;

        size_t break_count = 0;

        // 1. Check each Front-Office record against Back-Office
        for (size_t i = 0; i < num_fo_; ++i) {
            auto& fo = fo_trades_[i];
            ExecutionEntry* bo_match = nullptr;

            for (size_t j = 0; j < num_bo_; ++j) {
                if (bo_trades_[j].order_id == fo.order_id) {
                    bo_match = &bo_trades_[j];
                    break;
                }
            }

            if (!bo_match) {
                if (break_count < max_breaks) {
                    out_breaks[break_count++] = ReconBreakReport{
                        .order_id = fo.order_id,
                        .break_type = BreakType::kUnmatchedFrontOffice,
                        .fo_qty = fo.qty,
                        .bo_qty = 0,
                        .fo_price = fo.price,
                        .bo_price = 0
                    };
                }
                continue;
            }

            fo.matched = true;
            bo_match->matched = true;

            // Check field discrepancies
            if (fo.qty != bo_match->qty) {
                if (break_count < max_breaks) {
                    out_breaks[break_count++] = ReconBreakReport{
                        .order_id = fo.order_id,
                        .break_type = BreakType::kQuantityBreak,
                        .fo_qty = fo.qty,
                        .bo_qty = bo_match->qty,
                        .fo_price = fo.price,
                        .bo_price = bo_match->price
                    };
                }
            } else if (fo.price != bo_match->price) {
                if (break_count < max_breaks) {
                    out_breaks[break_count++] = ReconBreakReport{
                        .order_id = fo.order_id,
                        .break_type = BreakType::kPriceBreak,
                        .fo_qty = fo.qty,
                        .bo_qty = bo_match->qty,
                        .fo_price = fo.price,
                        .bo_price = bo_match->price
                    };
                }
            }
        }

        // 2. Check for unmatched Back-Office records
        for (size_t j = 0; j < num_bo_; ++j) {
            if (!bo_trades_[j].matched && break_count < max_breaks) {
                out_breaks[break_count++] = ReconBreakReport{
                    .order_id = bo_trades_[j].order_id,
                    .break_type = BreakType::kUnmatchedBackOffice,
                    .fo_qty = 0,
                    .bo_qty = bo_trades_[j].qty,
                    .fo_price = 0,
                    .bo_price = bo_trades_[j].price
                };
            }
        }

        return break_count;
    }

private:
    std::array<ExecutionEntry, kMaxRecords> fo_trades_{};
    std::array<ExecutionEntry, kMaxRecords> bo_trades_{};
    size_t num_fo_{0};
    size_t num_bo_{0};
};

} // namespace recon
} // namespace luv
