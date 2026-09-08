#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace luv {

enum class IssuerFilingStatus : uint8_t {
    Current = 0,         // Timely SEC/regulatory filings (10-K, 10-Q)
    Delinquent = 1,      // Late or missing financial information
    ExemptADR = 2        // Foreign private issuer exempt under 12g3-2(b)
};

struct OtcSecurityState {
    uint64_t symbol_id{0};
    IssuerFilingStatus filing_status{IssuerFilingStatus::Current};
    uint32_t days_since_last_filing{0};     // Max allowed is 180 days for current status
    uint32_t consecutive_days_unquoted{0};   // For Piggyback Exception: max 4 consecutive business days
    bool is_piggyback_eligible{true};
    bool is_unsolicited_customer_order{false}; // Unsolicited customer order exemption under 15c2-11(f)(2)
};

class Otc15c211Validator {
public:
    static constexpr uint32_t kMaxFilingAgeDays = 180;
    static constexpr uint32_t kMaxUnquotedDaysPiggyback = 4;

    // Checks whether publishing a quotation (or entering proprietary quote) is legally permitted under SEC Rule 15c2-11
    static bool is_quotation_permitted(const OtcSecurityState& state) noexcept {
        // 1. Unsolicited customer orders are exempt under Rule 15c2-11(f)(2)
        if (state.is_unsolicited_customer_order) {
            return true;
        }

        // 2. Foreign exempt ADRs
        if (state.filing_status == IssuerFilingStatus::ExemptADR) {
            return true;
        }

        // 3. Information review requirement: issuer must have current publicly available financial filings
        if (state.filing_status == IssuerFilingStatus::Delinquent || state.days_since_last_filing > kMaxFilingAgeDays) {
            return false; // Delinquent financials block quotation publication
        }

        // 4. Piggyback exception check: quote must not have lapsed for >4 consecutive business days
        if (state.is_piggyback_eligible) {
            if (state.consecutive_days_unquoted > kMaxUnquotedDaysPiggyback) {
                return false; // Piggyback lapsed, full information review required before new quotes
            }
            return true;
        }

        return true;
    }
};

} // namespace luv
