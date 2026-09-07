#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "luv_execution.hpp"

namespace luv::ouch {

enum class EventType : uint8_t {
    kAccepted,
    kRejected,
    kExecuted,
    kCanceled,
    kReplaced,
};

struct Event {
    EventType type = EventType::kRejected;
    uint64_t order_id = 0;
    int64_t quantity = 0;
    int64_t price = 0;
    uint8_t reason = 0;
};

namespace detail {
inline uint32_t be32(const uint8_t* data) noexcept {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

inline uint64_t be64(const uint8_t* data) noexcept {
    uint64_t value = 0;
    for (uint32_t index = 0; index < 8; ++index)
        value = (value << 8) | data[index];
    return value;
}
}

class Parser {
public:
    // Messages are framed as [uint16 big-endian body length][body].
    // Body formats used by this adapter:
    // A: accepted, order id at 1..8
    // J: rejected, order id at 1..8, reason at 9
    // E: executed, order id at 1..8, quantity at 9..12
    // C: canceled, order id at 1..8
    // U: replaced, order id at 1..8, quantity at 9..12, price at 13..16
    [[nodiscard]] bool parse(const uint8_t* data, size_t length,
                             Event& event, size_t& consumed) const noexcept {
        consumed = 0;
        if (!data || length < 2) return false;
        const size_t body_length =
            (static_cast<size_t>(data[0]) << 8) | data[1];
        if (body_length < 9 || body_length > kMaxBodyLength ||
            length < body_length + 2) return false;

        const uint8_t* body = data + 2;
        Event parsed{};
        parsed.order_id = detail::be64(body + 1);
        if (parsed.order_id == 0) return false;

        switch (body[0]) {
        case 'A':
            if (body_length != kAcceptedLength) return false;
            parsed.type = EventType::kAccepted;
            break;
        case 'J':
            if (body_length != kRejectedLength) return false;
            parsed.type = EventType::kRejected;
            parsed.reason = body[9];
            break;
        case 'E':
            if (body_length != kExecutedLength) return false;
            parsed.type = EventType::kExecuted;
            parsed.quantity = static_cast<int64_t>(detail::be32(body + 9));
            if (parsed.quantity <= 0) return false;
            break;
        case 'C':
            if (body_length != kCanceledLength) return false;
            parsed.type = EventType::kCanceled;
            break;
        case 'U':
            if (body_length != kReplacedLength) return false;
            parsed.type = EventType::kReplaced;
            parsed.quantity = static_cast<int64_t>(detail::be32(body + 9));
            parsed.price = static_cast<int64_t>(detail::be32(body + 13));
            if (parsed.quantity <= 0 || parsed.price <= 0) return false;
            break;
        default:
            return false;
        }

        event = parsed;
        consumed = body_length + 2;
        return true;
    }

    static constexpr size_t kAcceptedLength = 9;
    static constexpr size_t kRejectedLength = 10;
    static constexpr size_t kExecutedLength = 13;
    static constexpr size_t kCanceledLength = 9;
    static constexpr size_t kReplacedLength = 17;
    static constexpr size_t kMaxBodyLength = kReplacedLength;
};

class OrderFlowAdapter {
public:
    [[nodiscard]] bool consume(const uint8_t* data, size_t length,
                               uint16_t symbol, ExecutionGateway& gateway,
                               size_t& consumed) noexcept {
        Event event{};
        if (!_parser.parse(data, length, event, consumed)) return false;
        if (event.type == EventType::kExecuted)
            return gateway.apply_execution_report(
                symbol, ExecutionReport{event.order_id, event.quantity, false});
        if (event.type == EventType::kCanceled)
            return gateway.apply_cancel_report(symbol, event.order_id);
        if (event.type == EventType::kReplaced)
            return gateway.apply_replace_report(
                symbol, event.order_id, event.quantity, event.price);
        if (event.type == EventType::kRejected)
            return gateway.apply_reject_report(symbol, event.order_id, event.reason);
        return event.type == EventType::kAccepted;
    }

private:
    Parser _parser{};
};

}  // namespace luv::ouch
