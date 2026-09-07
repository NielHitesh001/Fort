#pragma once

// LUV execution and pre-trade risk gateway.
//
// The critical path is intentionally boring: integer comparisons, bitwise
// combination of pass bits, then fixed-offset binary packet patching. Setup can
// do richer work; order-time code must not allocate or build strings.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "luv_arena.hpp"
#include "luv_recovery.hpp"
#include "luv_safety.hpp"

namespace luv {

namespace exec {

enum Side : uint8_t {
    kBuy = 0,
    kSell = 1,
};

enum Reject : uint8_t {
    kRejectNone = 0,
    kRejectQty = 1u << 0,
    kRejectPosition = 1u << 1,
    kRejectStaleAlpha = 1u << 2,
    kRejectHalted = 1u << 3,
    kRejectFlatSignal = 1u << 4,
    kRejectOrderCapacity = 1u << 5,
    kRejectInvalidSymbol = 1u << 6,
    kRejectPrice = 1u << 7,
};

enum TimeInForce : uint8_t {
    kDay = 0,
    kIOC = 1, // Immediate Or Cancel
    kFOK = 2, // Fill Or Kill
    kGTC = 3, // Good Till Cancel
};

enum class TriggerType : uint8_t {
    kStopLoss = 0,   // Market order triggered when market crosses stop_price
    kStopLimit = 1,  // Limit order at limit_price triggered when market crosses stop_price
    kPeggedBid = 2,  // Pegged to Best Bid + offset
    kPeggedAsk = 3,  // Pegged to Best Ask + offset
    kPeggedMid = 4,  // Pegged to Midpoint + offset
};

struct ConditionalOrder {
    uint32_t trigger_id = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = kBuy;
    TriggerType trigger_type = TriggerType::kStopLoss;
    int64_t stop_price = 0;
    int64_t limit_price = 0;
    int64_t peg_offset = 0;
    int64_t qty = 0;
    uint32_t client_order_id = 0;
    uint8_t time_in_force = kDay;
    uint8_t active = 0;
};

struct RiskLimits {
    int64_t max_order_qty = 1'000;
    int64_t max_abs_position = 100'000;
    uint64_t max_alpha_age_ns = 250'000; // 250 us
    int64_t min_price = 1;
    int64_t max_price = 10'000'000'000;
    int64_t reference_price = 0;       // 0 = collar check disabled
    int64_t max_price_collar_pct = 10; // 10% max collar deviation if reference_price > 0
    int64_t max_gross_loss = 0;        // 0 = disabled
    uint32_t max_consecutive_rejections = 5; // max rejections before per-symbol halt
};

struct OrderIntent {
    uint16_t symbol_idx = 0;
    uint8_t side = kBuy;
    int64_t qty = 0;
    int64_t price = 0; // fixed point x 10^4
    uint64_t alpha_timestamp_ns = 0;
    uint64_t now_ns = 0;
    uint32_t client_order_id = 0;
    uint8_t time_in_force = kDay;
};

struct OrderReplaceIntent {
    uint16_t symbol_idx = 0;
    uint32_t original_order_id = 0;
    uint32_t replacement_order_id = 0;
    int64_t new_qty = 0;
    int64_t new_price = 0;
    uint64_t alpha_timestamp_ns = 0;
    uint64_t now_ns = 0;
    uint8_t time_in_force = kDay;
};

struct RiskDecision {
    uint8_t pass = 0;        // 1 = accepted, 0 = rejected
    uint8_t reject_mask = 0; // exec::Reject bits
};

// Minimal Nasdaq OUCH-style Enter Order packet. Exact production venues may
// need extra fields, but the design is the important part: all mutable fields
// are fixed-offset binary writes.
struct alignas(64) OutboundPacket {
    uint16_t len = 0;
    uint8_t bytes[64] = {};
};
static_assert(sizeof(OutboundPacket) == 128,
              "OutboundPacket stays cache-line aligned for NIC handoff");

namespace ouch {
inline constexpr uint16_t kEnterOrderLen = 48;
inline constexpr uint16_t kReplaceOrderLen = 32;
inline constexpr uint32_t kMaxSymbolText = 8;

inline constexpr size_t kMsgTypeOffset = 0;
inline constexpr size_t kTokenOffset = 1;     // uint32 be
inline constexpr size_t kSideOffset = 5;      // 'B' or 'S'
inline constexpr size_t kQtyOffset = 6;       // uint32 be
inline constexpr size_t kSymbolOffset = 10;   // 8 ASCII bytes
inline constexpr size_t kPriceOffset = 18;    // uint32 be
inline constexpr size_t kTimeInForceOffset = 22;
inline constexpr size_t kFirmOffset = 26;
inline constexpr size_t kDisplayOffset = 30;
inline constexpr size_t kCapacityOffset = 31;
inline constexpr size_t kIntermarketSweepOffset = 32;
inline constexpr size_t kMinQtyOffset = 33;   // uint32 be
inline constexpr size_t kCrossTypeOffset = 37;
inline constexpr size_t kCustomerTypeOffset = 38;
inline constexpr size_t kReservedOffset = 39;

inline constexpr size_t kReplaceMsgTypeOffset = 0;
inline constexpr size_t kReplaceExistingTokenOffset = 1;     // uint32 be
inline constexpr size_t kReplaceReplacementTokenOffset = 5;  // uint32 be
inline constexpr size_t kReplaceQtyOffset = 9;               // uint32 be
inline constexpr size_t kReplacePriceOffset = 13;            // uint32 be
inline constexpr size_t kReplaceTimeInForceOffset = 17;      // uint32 be
inline constexpr size_t kReplaceDisplayOffset = 21;          // 'Y'
}  // namespace ouch

inline void store_u32_be(uint8_t* dst, uint32_t v) noexcept {
    dst[0] = static_cast<uint8_t>(v >> 24);
    dst[1] = static_cast<uint8_t>(v >> 16);
    dst[2] = static_cast<uint8_t>(v >> 8);
    dst[3] = static_cast<uint8_t>(v);
}

inline uint32_t clamp_u32(int64_t v) noexcept {
    const uint8_t non_negative = static_cast<uint8_t>(v >= 0);
    const uint8_t fits_u32 = static_cast<uint8_t>(
        static_cast<uint64_t>(v) <= 0xFFFF'FFFFULL);
    const uint8_t ok = non_negative & fits_u32;
    const uint32_t clipped = (v < 0) ? 0u : 0xFFFF'FFFFu;
    return ok ? static_cast<uint32_t>(v) : clipped;
}

inline int64_t signed_qty_delta(uint8_t side, int64_t qty) noexcept {
    const int64_t sign_mask = -static_cast<int64_t>(side & 1u);
    return (qty ^ sign_mask) - sign_mask; // buy=+qty, sell=-qty
}

inline uint64_t abs_i64(int64_t v) noexcept {
    const uint64_t bits = static_cast<uint64_t>(v);
    const uint64_t sign = bits >> 63;
    return (bits ^ (0u - sign)) + sign;
}

}  // namespace exec

using OutboundPacket = exec::OutboundPacket;

class PreTradeRisk {
public:
    PreTradeRisk() = default;
    PreTradeRisk(const PreTradeRisk&) = delete;
    PreTradeRisk& operator=(const PreTradeRisk&) = delete;

    [[nodiscard]] bool init(Arena& arena) noexcept {
        if (!arena.is_initialised()) return false;
        _arena = &arena;
        for (auto& lim : _limits) lim = exec::RiskLimits{};
        return true;
    }

    void set_limits(uint16_t sym, const exec::RiskLimits& limits) noexcept {
        if (sym >= Config::kSymbols) return;
        _limits[sym] = limits;
    }

    [[nodiscard]] const exec::RiskLimits& limits(uint16_t sym) const noexcept {
        return _limits[sym < Config::kSymbols ? sym : 0];
    }

    [[nodiscard]] exec::RiskDecision evaluate(
        const exec::OrderIntent& intent) const noexcept
    {
        if (!_arena || intent.symbol_idx >= Config::kSymbols) [[unlikely]] {
            return {0, exec::kRejectInvalidSymbol};
        }
        const exec::RiskLimits& limits = _limits[intent.symbol_idx];
        const RiskState& risk = _arena->exec_states[intent.symbol_idx].risk;

        const uint8_t valid_qty =
            static_cast<uint8_t>((intent.qty > 0) &
                                 (intent.qty <= limits.max_order_qty));
        // Never feed an invalid signed quantity into arithmetic that could
        // overflow before the order is rejected.
        const int64_t safe_qty = valid_qty ? intent.qty : 0;
        const int64_t signed_delta = exec::signed_qty_delta(intent.side, safe_qty);
        int64_t projected = 0;
        const uint8_t position_add_ok = static_cast<uint8_t>(
            !__builtin_add_overflow(risk.net_position, signed_delta, &projected));
        const uint64_t age_ns = intent.now_ns - intent.alpha_timestamp_ns;

        const uint8_t valid_pos =
            static_cast<uint8_t>(position_add_ok &
                (limits.max_abs_position >= 0) &
                (exec::abs_i64(projected) <=
                 static_cast<uint64_t>(limits.max_abs_position)));
        const uint8_t valid_alpha =
            static_cast<uint8_t>(age_ns <= limits.max_alpha_age_ns);
        const uint8_t valid_halt =
            static_cast<uint8_t>(risk.halted == 0);
        const uint8_t valid_price = static_cast<uint8_t>(
            (intent.price >= (limits.min_price > 0 ? limits.min_price : 1)) &
            (static_cast<uint64_t>(intent.price) <= static_cast<uint64_t>(limits.max_price > 0 ? limits.max_price : 0xFFFF'FFFFULL)) &
            (static_cast<uint64_t>(intent.price) <= 0xFFFF'FFFFULL));
        uint8_t collar_ok = 1;
        if (limits.reference_price > 0 && limits.max_price_collar_pct > 0) {
            const int64_t diff = (intent.price >= limits.reference_price)
                ? (intent.price - limits.reference_price)
                : (limits.reference_price - intent.price);
            const int64_t max_allowed_diff = (limits.reference_price * limits.max_price_collar_pct) / 100;
            collar_ok = static_cast<uint8_t>(diff <= max_allowed_diff);
        }
        const uint8_t valid_price_final = valid_price & collar_ok;

        const uint8_t loss_ok = static_cast<uint8_t>(
            limits.max_gross_loss <= 0 ||
            risk.current_drawdown <= limits.max_gross_loss);

        int64_t order_notional = 0;
        const uint8_t notional_mul_ok = static_cast<uint8_t>(
            !__builtin_mul_overflow(intent.price, safe_qty, &order_notional));
        int64_t gross_after_order = 0;
        const uint8_t exposure_add_ok = static_cast<uint8_t>(
            !__builtin_add_overflow(risk.gross_exposure, order_notional,
                                     &gross_after_order));
        const uint8_t valid_notional = static_cast<uint8_t>(
            valid_price_final & notional_mul_ok & exposure_add_ok &
            (risk.gross_exposure >= 0));

        const uint8_t pass = valid_qty & valid_pos & valid_alpha & valid_halt &
                             valid_price_final & valid_notional & loss_ok;
        const uint8_t reject =
            static_cast<uint8_t>((valid_qty ^ 1u) * exec::kRejectQty) |
            static_cast<uint8_t>((valid_pos ^ 1u) * exec::kRejectPosition) |
            static_cast<uint8_t>((valid_alpha ^ 1u) * exec::kRejectStaleAlpha) |
            static_cast<uint8_t>(((valid_halt & loss_ok) ^ 1u) * exec::kRejectHalted) |
            static_cast<uint8_t>(((valid_price_final & valid_notional) ^ 1u) *
                                 exec::kRejectPrice);
        const uint8_t fail_mask = static_cast<uint8_t>(pass - 1u);

        return {pass, static_cast<uint8_t>(reject & fail_mask)};
    }

private:
    Arena* _arena = nullptr;
    exec::RiskLimits _limits[Config::kSymbols];
};

class OuchPacketTemplates {
public:
    [[nodiscard]] bool init() noexcept {
        for (uint16_t sym = 0; sym < Config::kSymbols; ++sym) {
            init_template(sym, exec::kBuy);
            init_template(sym, exec::kSell);
        }
        _initialised = true;
        return true;
    }

    [[nodiscard]] bool build_enter_order(
        const exec::OrderIntent& intent,
        OutboundPacket& out) const noexcept
    {
        if (!_initialised || intent.symbol_idx >= Config::kSymbols)
            return false;

        const uint8_t side = intent.side & 1u;
        const auto& tmpl = _templates[intent.symbol_idx][side];
        std::memcpy(out.bytes, tmpl.data(), exec::ouch::kEnterOrderLen);
        out.len = exec::ouch::kEnterOrderLen;

        exec::store_u32_be(out.bytes + exec::ouch::kTokenOffset,
                           intent.client_order_id);
        exec::store_u32_be(out.bytes + exec::ouch::kQtyOffset,
                           exec::clamp_u32(intent.qty));
        exec::store_u32_be(out.bytes + exec::ouch::kPriceOffset,
                           exec::clamp_u32(intent.price));
        exec::store_u32_be(out.bytes + exec::ouch::kTimeInForceOffset,
                           static_cast<uint32_t>(intent.time_in_force));
        return true;
    }

    [[nodiscard]] bool build_replace_order(
        const exec::OrderReplaceIntent& intent,
        OutboundPacket& out) const noexcept
    {
        if (!_initialised || intent.symbol_idx >= Config::kSymbols)
            return false;

        out.len = exec::ouch::kReplaceOrderLen;
        std::memset(out.bytes, 0, sizeof(out.bytes));
        out.bytes[exec::ouch::kReplaceMsgTypeOffset] = 'U';
        exec::store_u32_be(out.bytes + exec::ouch::kReplaceExistingTokenOffset,
                           intent.original_order_id);
        exec::store_u32_be(out.bytes + exec::ouch::kReplaceReplacementTokenOffset,
                           intent.replacement_order_id);
        exec::store_u32_be(out.bytes + exec::ouch::kReplaceQtyOffset,
                           exec::clamp_u32(intent.new_qty));
        exec::store_u32_be(out.bytes + exec::ouch::kReplacePriceOffset,
                           exec::clamp_u32(intent.new_price));
        exec::store_u32_be(out.bytes + exec::ouch::kReplaceTimeInForceOffset,
                           static_cast<uint32_t>(intent.time_in_force));
        out.bytes[exec::ouch::kReplaceDisplayOffset] = 'Y';
        return true;
    }

private:
    using Template = std::array<uint8_t, exec::ouch::kEnterOrderLen>;

    void init_template(uint16_t sym, uint8_t side) noexcept {
        Template& t = _templates[sym][side];
        t.fill(0);

        t[exec::ouch::kMsgTypeOffset] = 'O';
        t[exec::ouch::kSideOffset] = (side == exec::kBuy) ? 'B' : 'S';
        make_symbol(sym, t.data() + exec::ouch::kSymbolOffset);

        exec::store_u32_be(t.data() + exec::ouch::kTimeInForceOffset, 0);
        t[exec::ouch::kFirmOffset + 0] = 'L';
        t[exec::ouch::kFirmOffset + 1] = 'U';
        t[exec::ouch::kFirmOffset + 2] = 'V';
        t[exec::ouch::kFirmOffset + 3] = ' ';
        t[exec::ouch::kDisplayOffset] = 'Y';
        t[exec::ouch::kCapacityOffset] = 'A';
        t[exec::ouch::kIntermarketSweepOffset] = 'N';
        exec::store_u32_be(t.data() + exec::ouch::kMinQtyOffset, 0);
        t[exec::ouch::kCrossTypeOffset] = 'N';
        t[exec::ouch::kCustomerTypeOffset] = ' ';
    }

    static void make_symbol(uint16_t sym, uint8_t* dst) noexcept {
        char tmp[exec::ouch::kMaxSymbolText + 1] = "        ";
        std::snprintf(tmp, sizeof(tmp), "S%03u    ", static_cast<unsigned>(sym));
        std::memcpy(dst, tmp, exec::ouch::kMaxSymbolText);
    }

    Template _templates[Config::kSymbols][2];
    bool _initialised = false;
};

class ExecutionGateway {
public:
    explicit ExecutionGateway(uint32_t rate_limit = 10'000) noexcept
        : _rate_limiter(rate_limit) {}

    [[nodiscard]] bool init(Arena& arena) noexcept {
        if (!arena.is_initialised()) return false;
        _arena = &arena;
        if (!_risk.init(arena)) return false;
        if (!_ouch.init()) return false;
        _breaker.reset();
        return true;
    }

    PreTradeRisk& risk() noexcept { return _risk; }
    const PreTradeRisk& risk() const noexcept { return _risk; }
    [[nodiscard]] CircuitBreaker& circuit_breaker() noexcept { return _breaker; }
    [[nodiscard]] const CircuitBreaker& circuit_breaker() const noexcept {
        return _breaker;
    }

    [[nodiscard]] exec::RiskDecision try_build(
        const exec::OrderIntent& intent,
        OutboundPacket& packet) noexcept
    {
        packet.len = 0;
        if (!_arena || intent.symbol_idx >= Config::kSymbols) [[unlikely]] {
            return {0, exec::kRejectInvalidSymbol};
        }
        if (!_breaker.allow(intent.now_ns)) {
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            _arena->exec_states[intent.symbol_idx].risk.halted = 1;
            return {0, exec::kRejectHalted};
        }
        if (!_rate_limiter.try_acquire()) {
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            _arena->exec_states[intent.symbol_idx].risk.halted = 1;
            return {0, exec::kRejectHalted};
        }

        exec::RiskDecision decision = _risk.evaluate(intent);
        const RiskState& risk = _arena->exec_states[intent.symbol_idx].risk;
        const uint8_t risk_pass = decision.pass;
        const uint8_t capacity_ok = static_cast<uint8_t>(
            risk.order_count < Config::kMaxActiveOrders);
        decision.pass &= capacity_ok;
        decision.reject_mask |= static_cast<uint8_t>(
            (capacity_ok ^ 1u) * exec::kRejectOrderCapacity);
        if (!risk_pass) {
            _rate_limiter.release();
            packet.len = 0;
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            return decision;
        }
        if (!capacity_ok) {
            _rate_limiter.release();
            _arena->exec_states[intent.symbol_idx].risk.halted = 1;
            packet.len = 0;
            return decision;
        }

        const bool built = decision.pass && _ouch.build_enter_order(intent, packet);
        const uint8_t built_bit = static_cast<uint8_t>(built);
        decision.pass &= built_bit;
        decision.reject_mask |= static_cast<uint8_t>(
            (built_bit ^ 1u) * exec::kRejectFlatSignal);

        if (!decision.pass) {
            _rate_limiter.release();
            packet.len = 0;
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            return decision;
        }

        const uint8_t pass_mask =
            static_cast<uint8_t>(-static_cast<int8_t>(decision.pass));
        packet.len = static_cast<uint16_t>(packet.len & pass_mask);

        if (decision.pass) [[likely]] {
            if (!_reconciliation.register_order(intent.client_order_id,
                                                intent.qty)) {
                _rate_limiter.release();
                decision.pass = 0;
                decision.reject_mask |= exec::kRejectPosition;
                packet.len = 0;
                ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
                _breaker.record_failure(intent.now_ns);
            } else if (!reserve_order_slot(intent)) {
                (void)_reconciliation.cancel(intent.client_order_id);
                _rate_limiter.release();
                decision.pass = 0;
                decision.reject_mask |= exec::kRejectPosition;
                packet.len = 0;
                _breaker.record_failure(intent.now_ns);
            } else if (_audit_log &&
                       !_audit_log->append(intent.now_ns,
                                           intent.client_order_id,
                                           intent.price, intent.qty,
                                           0, intent.side, 'A')) {
                (void)_reconciliation.cancel(intent.client_order_id);
                release_order_slot(intent);
                _rate_limiter.release();
                decision.pass = 0;
                decision.reject_mask |= exec::kRejectHalted;
                packet.len = 0;
                ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
                _breaker.record_failure(intent.now_ns);
            }
            if (decision.pass && _recovery_ledger &&
                !_recovery_ledger->append_and_flush(RecoveryEvent{
                    RecoveryEventType::kAdd,
                    intent.client_order_id,
                    intent.qty,
                    intent.price,
                    static_cast<uint8_t>(intent.side),
                    intent.now_ns})) {
                (void)_reconciliation.cancel(intent.client_order_id);
                release_order_slot(intent);
                _rate_limiter.release();
                decision.pass = 0;
                decision.reject_mask |= exec::kRejectHalted;
                packet.len = 0;
                ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
                _breaker.record_failure(intent.now_ns);
            }
        } else {
            _rate_limiter.release();
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
        }

        return decision;
    }

    [[nodiscard]] exec::RiskDecision try_replace(
        const exec::OrderReplaceIntent& intent,
        OutboundPacket& packet) noexcept
    {
        packet.len = 0;
        if (!_arena || intent.symbol_idx >= Config::kSymbols) [[unlikely]] {
            return {0, exec::kRejectInvalidSymbol};
        }
        if (!_breaker.allow(intent.now_ns)) {
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            _arena->exec_states[intent.symbol_idx].risk.halted = 1;
            return {0, exec::kRejectHalted};
        }
        if (!_reconciliation.contains(intent.original_order_id)) {
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            return {0, exec::kRejectPosition};
        }

        SymbolExecState& state = _arena->exec_states[intent.symbol_idx];
        const ActiveOrder* existing_order = nullptr;
        for (uint32_t i = 0; i < Config::kMaxActiveOrders; ++i) {
            if (state.orders[i].order_id == intent.original_order_id &&
                state.orders[i].state != 0 && state.orders[i].state != 3) {
                existing_order = &state.orders[i];
                break;
            }
        }
        if (!existing_order) {
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            return {0, exec::kRejectPosition};
        }

        exec::OrderIntent test_intent{};
        test_intent.symbol_idx = intent.symbol_idx;
        test_intent.side = existing_order->side;
        test_intent.qty = intent.new_qty;
        test_intent.price = intent.new_price;
        test_intent.alpha_timestamp_ns = intent.alpha_timestamp_ns;
        test_intent.now_ns = intent.now_ns;
        test_intent.client_order_id = intent.replacement_order_id;
        test_intent.time_in_force = intent.time_in_force;

        exec::RiskDecision decision = _risk.evaluate(test_intent);
        if (!decision.pass) {
            packet.len = 0;
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            return decision;
        }

        if (!_ouch.build_replace_order(intent, packet)) {
            packet.len = 0;
            decision.pass = 0;
            decision.reject_mask |= exec::kRejectFlatSignal;
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            return decision;
        }

        if (_audit_log &&
            !_audit_log->append(intent.now_ns,
                                intent.replacement_order_id,
                                intent.new_price, intent.new_qty,
                                0, existing_order->side, 'U')) {
            packet.len = 0;
            decision.pass = 0;
            decision.reject_mask |= exec::kRejectHalted;
            ++_arena->exec_states[intent.symbol_idx].risk.reject_count;
            _breaker.record_failure(intent.now_ns);
            return decision;
        }

        return decision;
    }

    void set_audit_log(DurableAuditLog* audit_log) noexcept {
        _audit_log = audit_log;
    }

    void set_recovery_ledger(RecoveryLedger* ledger) noexcept {
        _recovery_ledger = ledger;
    }

    [[nodiscard]] bool apply_execution_report(
        uint16_t symbol, const ExecutionReport& report) noexcept {
        if (!_arena || symbol >= Config::kSymbols) {
            if (_arena && symbol < Config::kSymbols) {
                _arena->exec_states[symbol].risk.halted = 1;
            }
            _breaker.trip();
            return false;
        }

        SymbolExecState& state = _arena->exec_states[symbol];
        for (uint32_t i = 0; i < Config::kMaxActiveOrders; ++i) {
            ActiveOrder& order = state.orders[i];
            if (order.order_id != report.order_id || order.state == 0)
                continue;

            if (report.filled_quantity <= 0 ||
                report.filled_quantity > order.qty ||
                !_reconciliation.contains(report.order_id)) {
                state.risk.halted = 1;
                _breaker.trip();
                return false;
            }

            if (_recovery_ledger &&
                !_recovery_ledger->append_and_flush(RecoveryEvent{
                    RecoveryEventType::kFill,
                    order.order_id,
                    static_cast<int64_t>(report.filled_quantity),
                    order.price,
                    static_cast<uint8_t>(order.side),
                    now_ns()})) {
                state.risk.halted = 1;
                _breaker.trip();
                return false;
            }

            if (!_reconciliation.apply(report)) {
                state.risk.halted = 1;
                _breaker.trip();
                return false;
            }

            order.filled_qty += report.filled_quantity;
            order.qty -= report.filled_quantity;
            if (report.terminal || order.qty == 0) {
                order.state = 3;
                if (state.risk.order_count > 0) --state.risk.order_count;
                _rate_limiter.release();
            } else {
                order.state = 2;
            }
            return true;
        }
        state.risk.halted = 1;
        _breaker.trip();
        return false;
    }

    [[nodiscard]] bool apply_cancel_report(
        uint16_t symbol, uint64_t order_id) noexcept {
        if (!_arena || symbol >= Config::kSymbols) {
            if (_arena && symbol < Config::kSymbols)
                _arena->exec_states[symbol].risk.halted = 1;
            _breaker.trip();
            return false;
        }
        SymbolExecState& state = _arena->exec_states[symbol];
        for (ActiveOrder& order : state.orders) {
            if (order.order_id != order_id || order.state == 0) continue;

            if (!_reconciliation.contains(order_id)) {
                state.risk.halted = 1;
                _breaker.trip();
                return false;
            }

            if (_recovery_ledger &&
                !_recovery_ledger->append_and_flush(RecoveryEvent{
                    RecoveryEventType::kCancel,
                    order.order_id,
                    order.qty,
                    order.price,
                    static_cast<uint8_t>(order.side),
                    now_ns()})) {
                state.risk.halted = 1;
                _breaker.trip();
                return false;
            }

            if (!_reconciliation.cancel(order_id)) {
                state.risk.halted = 1;
                _breaker.trip();
                return false;
            }

            order = ActiveOrder{};
            if (state.risk.order_count > 0) --state.risk.order_count;
            _rate_limiter.release();
            return true;
        }
        state.risk.halted = 1;
        _breaker.trip();
        return false;
    }

    [[nodiscard]] bool apply_replace_report(
        uint16_t symbol, uint64_t order_id, int64_t new_qty, int64_t new_price) noexcept {
        if (!_arena || symbol >= Config::kSymbols) {
            if (_arena && symbol < Config::kSymbols)
                _arena->exec_states[symbol].risk.halted = 1;
            _breaker.trip();
            return false;
        }
        SymbolExecState& state = _arena->exec_states[symbol];
        for (ActiveOrder& order : state.orders) {
            if (order.order_id != order_id || order.state == 0 || order.state == 3) continue;

            if (!_reconciliation.contains(order_id) || new_qty <= 0 || new_price <= 0) {
                state.risk.halted = 1;
                _breaker.trip();
                return false;
            }

            if (_recovery_ledger &&
                !_recovery_ledger->append_and_flush(RecoveryEvent{
                    RecoveryEventType::kReplace,
                    order.order_id,
                    new_qty,
                    new_price,
                    static_cast<uint8_t>(order.side),
                    now_ns()})) {
                state.risk.halted = 1;
                _breaker.trip();
                return false;
            }

            if (!_reconciliation.replace(order_id, new_qty)) {
                state.risk.halted = 1;
                _breaker.trip();
                return false;
            }

            const int64_t old_signed = exec::signed_qty_delta(order.side, order.qty);
            const int64_t new_signed = exec::signed_qty_delta(order.side, new_qty);
            state.risk.net_position += (new_signed - old_signed);
            state.risk.gross_exposure += (new_price * new_qty) - (order.price * order.qty);

            order.qty = new_qty;
            order.price = new_price;
            return true;
        }
        state.risk.halted = 1;
        _breaker.trip();
        return false;
    }

    void halt_symbol(uint16_t sym) noexcept {
        if (_arena && sym < Config::kSymbols) {
            _arena->exec_states[sym].risk.halted = 1;
        }
    }

    void resume_symbol(uint16_t sym) noexcept {
        if (_arena && sym < Config::kSymbols) {
            _arena->exec_states[sym].risk.halted = 0;
        }
    }

    [[nodiscard]] bool is_symbol_halted(uint16_t sym) const noexcept {
        if (!_arena || sym >= Config::kSymbols) return true;
        return _arena->exec_states[sym].risk.halted != 0;
    }

    [[nodiscard]] bool apply_reject_report(
        uint16_t symbol, uint64_t order_id, uint8_t reason = 0) noexcept {
        (void)reason;
        if (!_arena || symbol >= Config::kSymbols) {
            _breaker.record_failure(now_ns());
            return false;
        }
        SymbolExecState& state = _arena->exec_states[symbol];
        ++state.risk.reject_count;
        _breaker.record_rejection(now_ns());
        if (state.risk.reject_count >= _risk.limits(symbol).max_consecutive_rejections) {
            state.risk.halted = 1;
        }
        for (ActiveOrder& order : state.orders) {
            if (order.order_id == order_id && order.state != 0) {
                (void)_reconciliation.cancel(order_id);
                state.risk.net_position -= exec::signed_qty_delta(order.side, order.qty);
                state.risk.gross_exposure -= order.price * order.qty;
                if (state.risk.order_count > 0) --state.risk.order_count;
                order = ActiveOrder{};
                _rate_limiter.release();
                return true;
            }
        }
        return false;
    }

private:
    static uint64_t now_ns() noexcept {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }
    [[nodiscard]] bool reserve_order_slot(
        const exec::OrderIntent& intent) noexcept {
        SymbolExecState& state = _arena->exec_states[intent.symbol_idx];
        if (state.risk.order_count >= Config::kMaxActiveOrders) {
            state.risk.halted = 1;
            ++state.risk.reject_count;
            return false;
        }
        uint32_t slot = Config::kMaxActiveOrders;
        for (uint32_t i = 0; i < Config::kMaxActiveOrders; ++i) {
            if (state.orders[i].state == 0 || state.orders[i].state == 3) {
                slot = i;
                break;
            }
        }
        if (slot == Config::kMaxActiveOrders) return false;
        ActiveOrder& order = state.orders[slot];
        order.order_id = intent.client_order_id;
        order.price = intent.price;
        order.qty = intent.qty;
        order.filled_qty = 0;
        order.symbol_idx = intent.symbol_idx;
        order.side = intent.side & 1u;
        order.state = 1;
        order.time_in_force = intent.time_in_force;

        state.risk.net_position +=
            exec::signed_qty_delta(intent.side, intent.qty);
        state.risk.gross_exposure += intent.price * intent.qty;
        ++state.risk.order_count;
        return true;
    }

    void release_order_slot(const exec::OrderIntent& intent) noexcept {
        SymbolExecState& state = _arena->exec_states[intent.symbol_idx];
        for (uint32_t i = 0; i < Config::kMaxActiveOrders; ++i) {
            if (state.orders[i].order_id != intent.client_order_id) continue;
            state.orders[i] = ActiveOrder{};
            if (state.risk.order_count > 0) --state.risk.order_count;
            state.risk.net_position -=
                exec::signed_qty_delta(intent.side, intent.qty);
            state.risk.gross_exposure -= intent.price * intent.qty;
            return;
        }
    }

    Arena* _arena = nullptr;
    PreTradeRisk _risk;
    OuchPacketTemplates _ouch;
    CircuitBreaker _breaker{1};
    ReconciliationLedger<> _reconciliation{};
    OrderRateLimiter _rate_limiter;
    DurableAuditLog* _audit_log = nullptr;
    RecoveryLedger* _recovery_ledger = nullptr;
};

template <size_t MaxTriggers = 1024>
class StopOrderTable {
public:
    StopOrderTable() noexcept {
        for (auto& t : _triggers) t = exec::ConditionalOrder{};
    }

    [[nodiscard]] bool register_trigger(const exec::ConditionalOrder& order) noexcept {
        if (order.client_order_id == 0 || order.qty <= 0 || order.symbol_idx >= Config::kSymbols)
            return false;
        for (size_t i = 0; i < MaxTriggers; ++i) {
            if (_triggers[i].active == 0) {
                _triggers[i] = order;
                _triggers[i].active = 1;
                _triggers[i].trigger_id = static_cast<uint32_t>(i + 1);
                ++_active_count;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool cancel_trigger(uint32_t client_order_id) noexcept {
        if (client_order_id == 0) return false;
        for (size_t i = 0; i < MaxTriggers; ++i) {
            if (_triggers[i].active && _triggers[i].client_order_id == client_order_id) {
                _triggers[i].active = 0;
                if (_active_count > 0) --_active_count;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] size_t active_count() const noexcept { return _active_count; }

    [[nodiscard]] const exec::ConditionalOrder* get(size_t index) const noexcept {
        return (index < MaxTriggers && _triggers[index].active) ? &_triggers[index] : nullptr;
    }

    size_t evaluate_bbo(uint16_t symbol, int64_t best_bid, int64_t best_ask,
                        uint64_t now_ns, ExecutionGateway& gateway,
                        OutboundPacket* out_packets, size_t max_packets) noexcept
    {
        if (symbol >= Config::kSymbols || _active_count == 0 || !out_packets || max_packets == 0)
            return 0;

        size_t routed = 0;
        for (size_t i = 0; i < MaxTriggers && routed < max_packets; ++i) {
            exec::ConditionalOrder& trig = _triggers[i];
            if (!trig.active || trig.symbol_idx != symbol) continue;

            bool triggered = false;
            int64_t order_price = trig.limit_price;

            switch (trig.trigger_type) {
            case exec::TriggerType::kStopLoss:
                if (trig.side == exec::kBuy && best_ask > 0 && best_ask >= trig.stop_price) {
                    triggered = true;
                    order_price = best_ask;
                } else if (trig.side == exec::kSell && best_bid > 0 && best_bid <= trig.stop_price) {
                    triggered = true;
                    order_price = best_bid;
                }
                break;
            case exec::TriggerType::kStopLimit:
                if (trig.side == exec::kBuy && best_ask > 0 && best_ask >= trig.stop_price) {
                    triggered = true;
                    order_price = trig.limit_price > 0 ? trig.limit_price : best_ask;
                } else if (trig.side == exec::kSell && best_bid > 0 && best_bid <= trig.stop_price) {
                    triggered = true;
                    order_price = trig.limit_price > 0 ? trig.limit_price : best_bid;
                }
                break;
            case exec::TriggerType::kPeggedBid:
                if (best_bid > 0) {
                    triggered = true;
                    order_price = best_bid + trig.peg_offset;
                }
                break;
            case exec::TriggerType::kPeggedAsk:
                if (best_ask > 0) {
                    triggered = true;
                    order_price = best_ask + trig.peg_offset;
                }
                break;
            case exec::TriggerType::kPeggedMid:
                if (best_bid > 0 && best_ask > 0) {
                    triggered = true;
                    order_price = ((best_bid + best_ask) / 2) + trig.peg_offset;
                }
                break;
            }

            if (triggered && order_price > 0) {
                exec::OrderIntent intent{};
                intent.symbol_idx = trig.symbol_idx;
                intent.side = trig.side;
                intent.qty = trig.qty;
                intent.price = order_price;
                intent.alpha_timestamp_ns = now_ns;
                intent.now_ns = now_ns;
                intent.client_order_id = trig.client_order_id;
                intent.time_in_force = trig.time_in_force;

                const exec::RiskDecision decision = gateway.try_build(intent, out_packets[routed]);
                if (decision.pass) {
                    trig.active = 0;
                    if (_active_count > 0) --_active_count;
                    ++routed;
                }
            }
        }
        return routed;
    }

private:
    exec::ConditionalOrder _triggers[MaxTriggers];
    size_t _active_count = 0;
};

}  // namespace luv
