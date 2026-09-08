#pragma once

#include <cstdint>
#include <array>
#include <cstring>
#include "luv_execution.hpp"

namespace luv {
namespace reg_sho {

struct LocatePool {
    uint16_t symbol_idx = 0;
    int64_t available_shares = 0;
    int64_t allocated_shares = 0;
    double borrow_fee_bps = 50.0; // Standard 50 bps for easy-to-borrow
    bool is_hard_to_borrow = false;
    uint32_t active_locates = 0;
};

struct LocateAllocation {
    uint64_t locate_id = 0;
    uint32_t client_id = 0;
    uint16_t symbol_idx = 0;
    int64_t shares = 0;
    double borrow_fee_bps = 0.0;
    uint64_t expiry_ts_ns = 0;
    bool active = false;
};

class ShortSaleLocateEngine {
public:
    static constexpr size_t kMaxSymbols = 128;
    static constexpr size_t kMaxAllocations = 1024;
    static constexpr uint64_t kDefaultLocateDurationNs = 86'400'000'000'000ULL; // 24 hours (1 trading day)

    ShortSaleLocateEngine() noexcept : num_pools_(0), num_allocations_(0) {}

    // Register or update borrow pool for a symbol
    bool set_borrow_inventory(
        uint16_t symbol_idx,
        int64_t total_shares,
        double base_fee_bps,
        bool htb) noexcept
    {
        for (size_t i = 0; i < num_pools_; ++i) {
            if (pools_[i].symbol_idx == symbol_idx) {
                pools_[i].available_shares = total_shares - pools_[i].allocated_shares;
                pools_[i].borrow_fee_bps = base_fee_bps;
                pools_[i].is_hard_to_borrow = htb;
                return true;
            }
        }
        if (num_pools_ < kMaxSymbols) {
            pools_[num_pools_++] = LocatePool{
                .symbol_idx = symbol_idx,
                .available_shares = total_shares,
                .allocated_shares = 0,
                .borrow_fee_bps = base_fee_bps,
                .is_hard_to_borrow = htb,
                .active_locates = 0
            };
            return true;
        }
        return false;
    }

    // Requests a bona fide short sale locate (Reg SHO Rule 203)
    bool request_locate(
        uint64_t locate_id,
        uint32_t client_id,
        uint16_t symbol_idx,
        int64_t requested_shares,
        uint64_t now_ns,
        LocateAllocation& out_allocation,
        const char** out_err_reason = nullptr) noexcept
    {
        if (requested_shares <= 0) {
            if (out_err_reason) *out_err_reason = "Requested locate shares must be > 0";
            return false;
        }

        LocatePool* pool = nullptr;
        for (size_t i = 0; i < num_pools_; ++i) {
            if (pools_[i].symbol_idx == symbol_idx) {
                pool = &pools_[i];
                break;
            }
        }

        if (!pool) {
            if (out_err_reason) *out_err_reason = "No borrow inventory registered for symbol (Locate Failed)";
            return false;
        }

        if (pool->available_shares < requested_shares) {
            if (out_err_reason) *out_err_reason = "Insufficient borrow shares available (Locate Denied)";
            return false;
        }

        if (num_allocations_ >= kMaxAllocations) {
            if (out_err_reason) *out_err_reason = "Locate table capacity reached";
            return false;
        }

        // Deduct from pool and add allocation
        pool->available_shares -= requested_shares;
        pool->allocated_shares += requested_shares;
        pool->active_locates++;

        out_allocation = LocateAllocation{
            .locate_id = locate_id,
            .client_id = client_id,
            .symbol_idx = symbol_idx,
            .shares = requested_shares,
            .borrow_fee_bps = pool->borrow_fee_bps,
            .expiry_ts_ns = now_ns + kDefaultLocateDurationNs,
            .active = true
        };

        allocations_[num_allocations_++] = out_allocation;
        return true;
    }

    // Validates if an inbound short sale order has a valid locate
    bool validate_short_order(
        uint32_t client_id,
        uint16_t symbol_idx,
        int64_t order_qty,
        uint64_t now_ns) const noexcept
    {
        int64_t valid_locate_shares = 0;
        for (size_t i = 0; i < num_allocations_; ++i) {
            if (allocations_[i].active &&
                allocations_[i].client_id == client_id &&
                allocations_[i].symbol_idx == symbol_idx &&
                allocations_[i].expiry_ts_ns > now_ns)
            {
                valid_locate_shares += allocations_[i].shares;
            }
        }
        return valid_locate_shares >= order_qty;
    }

private:
    std::array<LocatePool, kMaxSymbols> pools_{};
    std::array<LocateAllocation, kMaxAllocations> allocations_{};
    size_t num_pools_{0};
    size_t num_allocations_{0};
};

} // namespace reg_sho
} // namespace luv
