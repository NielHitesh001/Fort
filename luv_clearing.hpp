#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <string_view>
#include "luv_execution.hpp"

namespace luv {
namespace clearing {

struct TradeRecord {
    uint64_t trade_id = 0;
    uint32_t buy_broker_id = 0;
    uint32_t sell_broker_id = 0;
    uint16_t symbol_idx = 0;
    int64_t price = 0;
    int64_t qty = 0;
    uint64_t timestamp_ns = 0;
};

// Continuous Net Settlement (CNS) aggregate position per clearing member
struct BrokerNetPosition {
    uint32_t broker_id = 0;
    uint16_t symbol_idx = 0;
    int64_t net_qty = 0;     // Positive = Long (Receivable), Negative = Short (Deliverable)
    int64_t net_cash = 0;    // Positive = Cash Receivable, Negative = Cash Payable
    uint64_t gross_trades = 0;
};

class ClearingHouseEngine {
public:
    static constexpr size_t kMaxBrokers = 64;

    ClearingHouseEngine() noexcept : num_brokers_(0) {}

    // Ingests executed trade and updates Continuous Net Settlement (CNS) accounts
    bool record_and_net_trade(const TradeRecord& trade) noexcept {
        if (trade.qty <= 0 || trade.price <= 0) return false;

        const int64_t settlement_cash = (trade.qty * trade.price) / 10000; // Scaled dollar amount

        // Update Buyer account
        auto* buyer = find_or_create(trade.buy_broker_id, trade.symbol_idx);
        if (!buyer) return false;
        buyer->net_qty += trade.qty;
        buyer->net_cash -= settlement_cash; // Buyer pays cash
        buyer->gross_trades++;

        // Update Seller account
        auto* seller = find_or_create(trade.sell_broker_id, trade.symbol_idx);
        if (!seller) return false;
        seller->net_qty -= trade.qty;
        seller->net_cash += settlement_cash; // Seller receives cash
        seller->gross_trades++;

        return true;
    }

    // Formats ISO 20022 setr.016 Securities Settlement Transaction Confirmation XML
    static size_t format_iso20022_setr016(
        char* out_buf,
        size_t max_len,
        uint64_t trade_id,
        std::string_view buyer_bic,
        std::string_view seller_bic,
        std::string_view isin,
        int64_t price,
        int64_t qty,
        uint64_t ts_ns) noexcept
    {
        if (!out_buf || max_len < 512) return 0;

        int written = std::snprintf(
            out_buf, max_len,
            "<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:setr.016.001.04\">"
            "<SctiesSttlmTxConf>"
            "<TxId>%llu</TxId>"
            "<TradDt>%llu</TradDt>"
            "<SttlmQty><Unit>%lld</Unit></SttlmQty>"
            "<DealPric><Pric><Val>%lld</Val></Pric></DealPric>"
            "<FinInstrmId><ISIN>%.*s</ISIN></FinInstrmId>"
            "<Buyr><Id><BIC>%.*s</BIC></Id></Buyr>"
            "<Sellr><Id><BIC>%.*s</BIC></Id></Sellr>"
            "</SctiesSttlmTxConf>"
            "</Document>",
            static_cast<unsigned long long>(trade_id),
            static_cast<unsigned long long>(ts_ns),
            static_cast<long long>(qty),
            static_cast<long long>(price),
            static_cast<int>(isin.size()), isin.data(),
            static_cast<int>(buyer_bic.size()), buyer_bic.data(),
            static_cast<int>(seller_bic.size()), seller_bic.data()
        );

        return (written > 0 && static_cast<size_t>(written) < max_len) ? static_cast<size_t>(written) : 0;
    }

    const BrokerNetPosition* get_broker_position(uint32_t broker_id, uint16_t symbol_idx) const noexcept {
        for (size_t i = 0; i < num_brokers_; ++i) {
            if (brokers_[i].broker_id == broker_id && brokers_[i].symbol_idx == symbol_idx) {
                return &brokers_[i];
            }
        }
        return nullptr;
    }

private:
    BrokerNetPosition* find_or_create(uint32_t broker_id, uint16_t symbol_idx) noexcept {
        for (size_t i = 0; i < num_brokers_; ++i) {
            if (brokers_[i].broker_id == broker_id && brokers_[i].symbol_idx == symbol_idx) {
                return &brokers_[i];
            }
        }
        if (num_brokers_ < kMaxBrokers) {
            brokers_[num_brokers_] = BrokerNetPosition{
                .broker_id = broker_id,
                .symbol_idx = symbol_idx,
                .net_qty = 0,
                .net_cash = 0,
                .gross_trades = 0
            };
            return &brokers_[num_brokers_++];
        }
        return nullptr;
    }

    std::array<BrokerNetPosition, kMaxBrokers> brokers_{};
    size_t num_brokers_{0};
};

} // namespace clearing
} // namespace luv
