#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "luv_execution.hpp"
#include "luv_lob.hpp"

namespace luv {

enum class GatewayMsgType : uint8_t {
    kNewOrderSingle = 1,
    kOrderCancel = 2,
    kOrderReplace = 3,
};

struct GatewayOrderRequest {
    GatewayMsgType type = GatewayMsgType::kNewOrderSingle;
    uint32_t client_order_id = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    uint8_t time_in_force = 0; // 0=Day, 1=IOC, 2=FOK, 3=GTC
    int64_t price = 0;
    int64_t qty = 0;
    uint64_t original_order_id = 0; // For cancel/replace
};

class OrderGatewayParser {
public:
    static bool parse_json_request(const char* json_str, GatewayOrderRequest& out_req) noexcept {
        if (!json_str) return false;

        // Fast field extraction without dynamic allocations
        const char* type_pos = std::strstr(json_str, "\"action\":");
        if (!type_pos) return false;

        if (std::strstr(type_pos, "\"NEW\"") || std::strstr(type_pos, "\"new\"")) {
            out_req.type = GatewayMsgType::kNewOrderSingle;
        } else if (std::strstr(type_pos, "\"CANCEL\"") || std::strstr(type_pos, "\"cancel\"")) {
            out_req.type = GatewayMsgType::kOrderCancel;
        } else if (std::strstr(type_pos, "\"REPLACE\"") || std::strstr(type_pos, "\"replace\"")) {
            out_req.type = GatewayMsgType::kOrderReplace;
        } else {
            return false;
        }

        const char* clordid_pos = std::strstr(json_str, "\"client_order_id\":");
        if (clordid_pos) {
            out_req.client_order_id = static_cast<uint32_t>(std::strtoul(clordid_pos + 18, nullptr, 10));
        }

        const char* sym_pos = std::strstr(json_str, "\"symbol_idx\":");
        if (sym_pos) {
            out_req.symbol_idx = static_cast<uint16_t>(std::strtoul(sym_pos + 13, nullptr, 10));
        }

        const char* side_pos = std::strstr(json_str, "\"side\":");
        if (side_pos) {
            if (std::strstr(side_pos, "\"BUY\"") || std::strstr(side_pos, "\"B\"")) {
                out_req.side = exec::kBuy;
            } else {
                out_req.side = exec::kSell;
            }
        }

        const char* price_pos = std::strstr(json_str, "\"price\":");
        if (price_pos) {
            out_req.price = static_cast<int64_t>(std::strtoll(price_pos + 8, nullptr, 10));
        }

        const char* qty_pos = std::strstr(json_str, "\"qty\":");
        if (qty_pos) {
            out_req.qty = static_cast<int64_t>(std::strtoll(qty_pos + 6, nullptr, 10));
        }

        return true;
    }

    static bool format_l2_snapshot_json(
        const LOBEngine& lob,
        uint16_t sym,
        uint32_t depth,
        char* buffer,
        size_t buffer_size) noexcept
    {
        if (!buffer || buffer_size == 0) return false;

        const int64_t best_bid = lob.best_bid_price(sym);
        const int64_t best_ask = lob.best_ask_price(sym);
        const int64_t bid_depth = lob.bid_depth_qty(sym, depth);
        const int64_t ask_depth = lob.ask_depth_qty(sym, depth);

        const int written = std::snprintf(
            buffer, buffer_size,
            "{\n"
            "  \"event\": \"L2_SNAPSHOT\",\n"
            "  \"symbol_idx\": %u,\n"
            "  \"best_bid\": %lld,\n"
            "  \"best_ask\": %lld,\n"
            "  \"bid_depth_qty\": %lld,\n"
            "  \"ask_depth_qty\": %lld\n"
            "}\n",
            sym,
            static_cast<long long>(best_bid),
            static_cast<long long>(best_ask),
            static_cast<long long>(bid_depth),
            static_cast<long long>(ask_depth));

        return written > 0 && static_cast<size_t>(written) < buffer_size;
    }
};

} // namespace luv
