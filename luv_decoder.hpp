#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <array>

#include "luv_arena.hpp"
#include "luv_decode_itch.hpp"

namespace luv {

enum class MarketDataProtocol : uint8_t {
    kITCH50 = 0,
    kSBE    = 1,
    kFIX    = 2,
    kFAST   = 3,
};

enum class AssetClass : uint8_t {
    kEquity  = 0,
    kCrypto  = 1,
    kFX      = 2,
    kFutures = 3,
};

struct InstrumentMetadata {
    uint16_t symbol_idx = 0;
    char ticker[8] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    AssetClass asset_class = AssetClass::kEquity;
    uint8_t price_decimals = 4; // default fixed-point x 10^4
    uint8_t qty_decimals = 0;   // 0 for integer shares, 8 for crypto
    int64_t min_tick_size = 100; // 0.0100 with 4 decimals
    int64_t min_lot_size = 1;
    int64_t contract_multiplier = 1;

    [[nodiscard]] int64_t to_fixed_price(double price) const noexcept {
        const double factor = std::pow(10.0, price_decimals);
        return static_cast<int64_t>(std::llround(price * factor));
    }

    [[nodiscard]] double to_float_price(int64_t fixed_price) const noexcept {
        const double factor = std::pow(10.0, price_decimals);
        return static_cast<double>(fixed_price) / factor;
    }

    [[nodiscard]] int64_t to_fixed_qty(double qty) const noexcept {
        const double factor = std::pow(10.0, qty_decimals);
        return static_cast<int64_t>(std::llround(qty * factor));
    }

    [[nodiscard]] double to_float_qty(int64_t fixed_qty) const noexcept {
        const double factor = std::pow(10.0, qty_decimals);
        return static_cast<double>(fixed_qty) / factor;
    }
};

class InstrumentRegistry {
public:
    InstrumentRegistry() noexcept {
        for (uint16_t i = 0; i < Config::kSymbols; ++i) {
            _instruments[i] = InstrumentMetadata{};
            _instruments[i].symbol_idx = i;
        }
    }

    [[nodiscard]] bool register_instrument(const InstrumentMetadata& meta) noexcept {
        if (meta.symbol_idx >= Config::kSymbols) return false;
        _instruments[meta.symbol_idx] = meta;
        return true;
    }

    [[nodiscard]] const InstrumentMetadata& get(uint16_t symbol_idx) const noexcept {
        return _instruments[symbol_idx < Config::kSymbols ? symbol_idx : 0];
    }

    [[nodiscard]] InstrumentMetadata& get(uint16_t symbol_idx) noexcept {
        return _instruments[symbol_idx < Config::kSymbols ? symbol_idx : 0];
    }

private:
    std::array<InstrumentMetadata, Config::kSymbols> _instruments{};
};

class IFeedDecoder {
public:
    virtual ~IFeedDecoder() = default;

    [[nodiscard]] virtual bool decode(
        const uint8_t* data, size_t length,
        SymbolTable& symbols, TickMsg& out_msg) noexcept = 0;

    [[nodiscard]] virtual MarketDataProtocol protocol() const noexcept = 0;
};

class ItchFeedDecoder final : public IFeedDecoder {
public:
    [[nodiscard]] bool decode(
        const uint8_t* data, size_t length,
        SymbolTable& symbols, TickMsg& out_msg) noexcept override {
        return decode_itch(data, length, symbols, out_msg);
    }

    [[nodiscard]] MarketDataProtocol protocol() const noexcept override {
        return MarketDataProtocol::kITCH50;
    }
};

// Simple Binary Encoding (SBE) Market Data Decoder
class SbeMarketDataDecoder final : public IFeedDecoder {
public:
    static constexpr uint16_t kHeaderLen = 8;
    static constexpr uint16_t kMsgAddOrder = 101;
    static constexpr uint16_t kMsgExecuted = 102;
    static constexpr uint16_t kMsgCanceled = 103;

    [[nodiscard]] bool decode(
        const uint8_t* data, size_t length,
        SymbolTable& symbols, TickMsg& out_msg) noexcept override
    {
        (void)symbols;
        if (!data || length < (kHeaderLen + 36)) return false;

        const uint8_t* body = data + kHeaderLen;
        uint64_t timestamp = 0;
        uint64_t order_ref = 0;
        int64_t price = 0;
        int64_t qty = 0;
        uint16_t symbol_idx = 0;
        uint8_t side = 0;
        uint8_t type_char = 'A';

        std::memcpy(&timestamp, body + 0, 8);
        std::memcpy(&order_ref, body + 8, 8);
        std::memcpy(&price, body + 16, 8);
        std::memcpy(&qty, body + 24, 8);
        std::memcpy(&symbol_idx, body + 32, 2);
        side = body[34];
        type_char = body[35];

        if (symbol_idx >= Config::kSymbols) return false;

        out_msg.msg_type = type_char;
        out_msg.flags = side & 1u;
        out_msg.symbol_idx = symbol_idx;
        out_msg.timestamp = timestamp;
        out_msg.price = price;
        out_msg.qty = qty;
        out_msg.order_ref = order_ref;
        out_msg.match_num = 0;
        return true;
    }

    [[nodiscard]] MarketDataProtocol protocol() const noexcept override {
        return MarketDataProtocol::kSBE;
    }
};

}  // namespace luv
