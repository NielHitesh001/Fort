#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <array>
#include <algorithm>
#include <string_view>

namespace luv {

enum class ShortSaleType : uint8_t {
    Long = 0,
    Short = 1,
    ShortExempt = 2
};

struct Rule201SymbolState {
    char symbol[12]{0};
    double prev_close_price{0.0};
    double circuit_breaker_trigger_price{0.0}; // prev_close * 0.90 (10% drop)
    bool circuit_breaker_active{false};
    uint64_t trigger_timestamp_ns{0};
    uint32_t active_until_day_index{0}; // Active for remainder of day + following trading day
};

struct ShortOrderValidationResult {
    bool order_permitted{false};
    bool rule_201_active{false};
    bool is_short_exempt{false};
    double minimum_permitted_price{0.0}; // Price must be strictly > NBB if Rule 201 active
    char violation_reason[64]{0};
};

class SECRule201UptickEngine {
public:
    static constexpr size_t MAX_SYMBOLS = 256;

    SECRule201UptickEngine() noexcept {
        reset();
    }

    void reset() noexcept {
        symbol_count_ = 0;
    }

    bool register_symbol(const char* symbol, double prev_close) noexcept {
        if (!symbol || prev_close <= 0.0 || symbol_count_ >= MAX_SYMBOLS) return false;

        for (size_t i = 0; i < symbol_count_; ++i) {
            if (std::strncmp(symbols_[i].symbol, symbol, sizeof(symbols_[i].symbol)) == 0) {
                symbols_[i].prev_close_price = prev_close;
                symbols_[i].circuit_breaker_trigger_price = prev_close * 0.90;
                symbols_[i].circuit_breaker_active = false;
                return true;
            }
        }

        auto& s = symbols_[symbol_count_++];
        std::strncpy(s.symbol, symbol, sizeof(s.symbol) - 1);
        s.prev_close_price = prev_close;
        s.circuit_breaker_trigger_price = prev_close * 0.90;
        s.circuit_breaker_active = false;
        return true;
    }

    // Process new trade/L1 quote price to detect 10% decline
    bool on_price_update(const char* symbol, double current_price, uint64_t timestamp_ns, uint32_t current_day_index) noexcept {
        Rule201SymbolState* s = find_symbol(symbol);
        if (!s) return false;

        if (!s->circuit_breaker_active && current_price <= s->circuit_breaker_trigger_price) {
            s->circuit_breaker_active = true;
            s->trigger_timestamp_ns = timestamp_ns;
            s->active_until_day_index = current_day_index + 1; // Remainder of current day + next day
            return true; // Newly triggered
        }

        return false;
    }

    // Day rollover: advance current day index and clear expired circuit breakers
    void on_day_rollover(uint32_t new_day_index) noexcept {
        for (size_t i = 0; i < symbol_count_; ++i) {
            if (symbols_[i].circuit_breaker_active && new_day_index > symbols_[i].active_until_day_index) {
                symbols_[i].circuit_breaker_active = false;
            }
        }
    }

    ShortOrderValidationResult validate_short_order(
        const char* symbol,
        ShortSaleType sale_type,
        double order_price,
        double national_best_bid,
        double tick_size = 0.01) const noexcept {

        ShortOrderValidationResult res{};

        if (sale_type == ShortSaleType::Long) {
            res.order_permitted = true;
            return res;
        }

        const Rule201SymbolState* s = find_symbol(symbol);
        if (!s) {
            res.order_permitted = true;
            return res;
        }

        res.rule_201_active = s->circuit_breaker_active;

        if (sale_type == ShortSaleType::ShortExempt) {
            res.is_short_exempt = true;
            res.order_permitted = true;
            return res;
        }

        if (!s->circuit_breaker_active) {
            // Unrestricted short sale when circuit breaker not active
            res.order_permitted = true;
            return res;
        }

        // Rule 201 Restriction: Short sale price must be strictly ABOVE the current National Best Bid (NBB)
        // Permitted price >= NBB + tick_size (or strictly > NBB)
        res.minimum_permitted_price = national_best_bid + tick_size;

        if (order_price <= national_best_bid) {
            res.order_permitted = false;
            std::strncpy(res.violation_reason, "RULE_201_PRICE_TEST_VIOLATION", sizeof(res.violation_reason) - 1);
            return res;
        }

        res.order_permitted = true;
        return res;
    }

    bool is_circuit_breaker_active(const char* symbol) const noexcept {
        const Rule201SymbolState* s = find_symbol(symbol);
        return s ? s->circuit_breaker_active : false;
    }

    size_t symbol_count() const noexcept { return symbol_count_; }

private:
    Rule201SymbolState* find_symbol(const char* symbol) noexcept {
        if (!symbol) return nullptr;
        for (size_t i = 0; i < symbol_count_; ++i) {
            if (std::strncmp(symbols_[i].symbol, symbol, sizeof(symbols_[i].symbol)) == 0) {
                return &symbols_[i];
            }
        }
        return nullptr;
    }

    const Rule201SymbolState* find_symbol(const char* symbol) const noexcept {
        if (!symbol) return nullptr;
        for (size_t i = 0; i < symbol_count_; ++i) {
            if (std::strncmp(symbols_[i].symbol, symbol, sizeof(symbols_[i].symbol)) == 0) {
                return &symbols_[i];
            }
        }
        return nullptr;
    }

    std::array<Rule201SymbolState, MAX_SYMBOLS> symbols_{};
    size_t symbol_count_{0};
};

} // namespace luv
