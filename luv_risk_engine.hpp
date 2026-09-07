#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "luv_safety.hpp"

namespace luv {

enum class RiskValidationResult : uint8_t {
    kApproved = 0,
    kRejected_ExceedsSingleNotional,
    kRejected_ExceedsSingleQuantity,
    kRejected_PriceCollarViolation,
    kRejected_ExceedsGrossNotional,
    kRejected_ExceedsNetPosition,
    kRejected_KillSwitchTripped,
    kRejected_SymbolHalted,
};

struct PreTradeRiskConfig {
    uint64_t max_single_order_notional = 1'000'000 * 10'000ULL; // $1,000,000 default
    int64_t max_single_order_quantity = 50'000;                // 50,000 units
    uint32_t price_collar_bps = 500;                            // 500 bps (5.00%)
    int64_t max_gross_notional = 50'000'000 * 10'000ULL;        // $50M gross exposure
    int64_t max_net_position_per_symbol = 100'000;              // 100,000 units net
    int64_t max_portfolio_drawdown = 250'000 * 10'000ULL;       // $250,000 drawdown
};

template <uint16_t NumSymbols = 256>
class AutonomousRiskEngine {
public:
    explicit AutonomousRiskEngine(const PreTradeRiskConfig& config = PreTradeRiskConfig{}) noexcept
        : _config(config) {}

    void set_config(const PreTradeRiskConfig& config) noexcept {
        _config = config;
    }

    [[nodiscard]] const PreTradeRiskConfig& config() const noexcept {
        return _config;
    }

    void trip_kill_switch(const char* reason = nullptr, uint64_t now_ns = 0) noexcept {
        _kill_switch_active.store(true, std::memory_order_release);
        _kill_switch_timestamp_ns.store(now_ns, std::memory_order_release);
        if (reason) {
            std::strncpy(_kill_switch_reason, reason, sizeof(_kill_switch_reason) - 1);
            _kill_switch_reason[sizeof(_kill_switch_reason) - 1] = '\0';
        }
    }

    void reset_kill_switch() noexcept {
        _kill_switch_active.store(false, std::memory_order_release);
        _kill_switch_timestamp_ns.store(0, std::memory_order_release);
        _kill_switch_reason[0] = '\0';
        _total_realized_loss.store(0, std::memory_order_release);
    }

    [[nodiscard]] bool is_kill_switch_active() const noexcept {
        return _kill_switch_active.load(std::memory_order_acquire);
    }

    [[nodiscard]] const char* kill_switch_reason() const noexcept {
        return _kill_switch_reason;
    }

    void halt_symbol(uint16_t symbol_idx) noexcept {
        if (symbol_idx < NumSymbols) {
            _symbol_halted[symbol_idx].store(true, std::memory_order_release);
        }
    }

    void resume_symbol(uint16_t symbol_idx) noexcept {
        if (symbol_idx < NumSymbols) {
            _symbol_halted[symbol_idx].store(false, std::memory_order_release);
        }
    }

    [[nodiscard]] bool is_symbol_halted(uint16_t symbol_idx) const noexcept {
        if (symbol_idx >= NumSymbols) return true;
        return _symbol_halted[symbol_idx].load(std::memory_order_acquire);
    }

    [[nodiscard]] RiskValidationResult validate_pre_trade(
        uint16_t symbol_idx, uint8_t side, int64_t price, int64_t quantity,
        int64_t ref_price = 0, uint64_t now_ns = 0) noexcept {
        (void)now_ns;

        if (_kill_switch_active.load(std::memory_order_acquire)) {
            return RiskValidationResult::kRejected_KillSwitchTripped;
        }

        if (symbol_idx >= NumSymbols || _symbol_halted[symbol_idx].load(std::memory_order_acquire)) {
            return RiskValidationResult::kRejected_SymbolHalted;
        }

        if (quantity <= 0 || quantity > _config.max_single_order_quantity) {
            return RiskValidationResult::kRejected_ExceedsSingleQuantity;
        }

        if (price <= 0) {
            return RiskValidationResult::kRejected_PriceCollarViolation;
        }

        const uint64_t order_notional = static_cast<uint64_t>(quantity) * static_cast<uint64_t>(price);
        if (_config.max_single_order_notional > 0 && order_notional > _config.max_single_order_notional) {
            return RiskValidationResult::kRejected_ExceedsSingleNotional;
        }

        // Fat-finger price collar check against reference price / BBO
        if (ref_price > 0 && _config.price_collar_bps > 0) {
            const int64_t diff = std::abs(price - ref_price);
            const int64_t max_allowed_diff = (ref_price * static_cast<int64_t>(_config.price_collar_bps)) / 10'000;
            if (diff > max_allowed_diff) {
                return RiskValidationResult::kRejected_PriceCollarViolation;
            }
        }

        // Portfolio Gross Notional check
        const int64_t current_gross = _portfolio_gross_notional.load(std::memory_order_relaxed);
        if (_config.max_gross_notional > 0 && (current_gross + static_cast<int64_t>(order_notional)) > _config.max_gross_notional) {
            return RiskValidationResult::kRejected_ExceedsGrossNotional;
        }

        // Net position limit check per symbol (side 0 = Buy, 1 = Sell)
        const int64_t current_pos = _symbol_net_positions[symbol_idx].load(std::memory_order_relaxed);
        const int64_t proposed_pos = (side == 0) ? (current_pos + quantity) : (current_pos - quantity);
        if (_config.max_net_position_per_symbol > 0 && std::abs(proposed_pos) > _config.max_net_position_per_symbol) {
            return RiskValidationResult::kRejected_ExceedsNetPosition;
        }

        return RiskValidationResult::kApproved;
    }

    void record_fill(uint16_t symbol_idx, uint8_t side, int64_t price, int64_t quantity,
                     int64_t pnl_delta = 0, uint64_t now_ns = 0) noexcept {
        if (symbol_idx >= NumSymbols || quantity <= 0 || price <= 0) return;

        const int64_t notional = quantity * price;
        _portfolio_gross_notional.fetch_add(notional, std::memory_order_relaxed);

        if (side == 0) { // Buy
            _symbol_net_positions[symbol_idx].fetch_add(quantity, std::memory_order_relaxed);
        } else { // Sell
            _symbol_net_positions[symbol_idx].fetch_sub(quantity, std::memory_order_relaxed);
        }

        if (pnl_delta < 0) {
            const int64_t loss = -pnl_delta;
            const int64_t total_loss = _total_realized_loss.fetch_add(loss, std::memory_order_acq_rel) + loss;
            if (_config.max_portfolio_drawdown > 0 && total_loss >= _config.max_portfolio_drawdown) {
                trip_kill_switch("Max portfolio drawdown threshold exceeded", now_ns);
            }
        }
    }

    [[nodiscard]] int64_t net_position(uint16_t symbol_idx) const noexcept {
        if (symbol_idx >= NumSymbols) return 0;
        return _symbol_net_positions[symbol_idx].load(std::memory_order_acquire);
    }

    [[nodiscard]] int64_t gross_notional() const noexcept {
        return _portfolio_gross_notional.load(std::memory_order_acquire);
    }

    [[nodiscard]] int64_t total_realized_loss() const noexcept {
        return _total_realized_loss.load(std::memory_order_acquire);
    }

private:
    PreTradeRiskConfig _config;
    std::atomic<bool> _kill_switch_active{false};
    std::atomic<uint64_t> _kill_switch_timestamp_ns{0};
    char _kill_switch_reason[128]{};
    std::atomic<int64_t> _portfolio_gross_notional{0};
    std::atomic<int64_t> _total_realized_loss{0};
    std::array<std::atomic<int64_t>, NumSymbols> _symbol_net_positions{};
    std::array<std::atomic<bool>, NumSymbols> _symbol_halted{};
};

} // namespace luv
