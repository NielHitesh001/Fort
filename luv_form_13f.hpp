#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <array>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace luv {

struct Form13fHolding {
    char cusip[10]{};
    char name_of_issuer[32]{};
    char title_of_class[16]{};
    uint64_t shares_or_prn_amount{0};
    uint64_t market_value_usd{0}; // In full USD (value rounded to thousands on filing)
    bool is_discretionary{true};
    bool is_defined{false};
};

class Form13fReporter {
public:
    static constexpr size_t kMaxHoldings = 128;
    static constexpr uint64_t kThresholdUsd = 100'000'000ULL; // $100M institutional manager threshold

    Form13fReporter() noexcept : holding_count_(0), total_market_value_(0) {}

    bool add_holding(
        std::string_view cusip,
        std::string_view issuer,
        std::string_view class_title,
        uint64_t shares,
        uint64_t market_val_usd) noexcept
    {
        if (holding_count_ >= kMaxHoldings) return false;

        auto& h = holdings_[holding_count_++];
        std::strncpy(h.cusip, cusip.data(), std::min(cusip.size(), sizeof(h.cusip) - 1));
        std::strncpy(h.name_of_issuer, issuer.data(), std::min(issuer.size(), sizeof(h.name_of_issuer) - 1));
        std::strncpy(h.title_of_class, class_title.data(), std::min(class_title.size(), sizeof(h.title_of_class) - 1));
        h.shares_or_prn_amount = shares;
        h.market_value_usd = market_val_usd;
        h.is_defined = true;

        total_market_value_ += market_val_usd;
        return true;
    }

    bool is_filing_required() const noexcept {
        return total_market_value_ >= kThresholdUsd;
    }

    uint64_t get_total_market_value() const noexcept {
        return total_market_value_;
    }

    size_t get_holding_count() const noexcept {
        return holding_count_;
    }

    // Zero-alloc XML/JSON report generator
    size_t export_xml_summary(char* out_buf, size_t max_len) const noexcept {
        if (!out_buf || max_len == 0) return 0;

        int written = std::snprintf(
            out_buf, max_len,
            "<edgarSubmission>\n"
            "  <form13fHeader>\n"
            "    <filingManagerThresholdMet>%s</filingManagerThresholdMet>\n"
            "    <totalValueUSD>%llu</totalValueUSD>\n"
            "    <entryCount>%zu</entryCount>\n"
            "  </form13fHeader>\n"
            "  <infoTable>\n",
            is_filing_required() ? "true" : "false",
            static_cast<unsigned long long>(total_market_value_),
            holding_count_
        );

        if (written < 0 || static_cast<size_t>(written) >= max_len) return 0;
        size_t offset = static_cast<size_t>(written);

        for (size_t i = 0; i < holding_count_; ++i) {
            const auto& h = holdings_[i];
            int entry_len = std::snprintf(
                out_buf + offset, max_len - offset,
                "    <infoTableEntry>\n"
                "      <nameOfIssuer>%s</nameOfIssuer>\n"
                "      <titleOfClass>%s</titleOfClass>\n"
                "      <cusip>%s</cusip>\n"
                "      <valueThousands>%llu</valueThousands>\n"
                "      <sshPrnamt>%llu</sshPrnamt>\n"
                "    </infoTableEntry>\n",
                h.name_of_issuer,
                h.title_of_class,
                h.cusip,
                static_cast<unsigned long long>(h.market_value_usd / 1000),
                static_cast<unsigned long long>(h.shares_or_prn_amount)
            );
            if (entry_len < 0 || offset + static_cast<size_t>(entry_len) >= max_len) break;
            offset += static_cast<size_t>(entry_len);
        }

        int tail = std::snprintf(out_buf + offset, max_len - offset, "  </infoTable>\n</edgarSubmission>\n");
        if (tail > 0 && offset + static_cast<size_t>(tail) < max_len) {
            offset += static_cast<size_t>(tail);
        }
        return offset;
    }

private:
    std::array<Form13fHolding, kMaxHoldings> holdings_{};
    size_t holding_count_{0};
    uint64_t total_market_value_{0};
};

} // namespace luv
