#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <algorithm>
#include <string_view>

namespace luv {

// Material Event Notice Types under SEC Rule 15c2-12(b)(5)(i)(C)
enum class MuniMaterialEventType : uint16_t {
    PrincipalAndInterestPaymentDelinquency = 1 << 0,
    NonPaymentRelatedDefault = 1 << 1,
    UnscheduledDrawOnDebtServiceReserve = 1 << 2,
    UnscheduledDrawOnCreditEnhancement = 1 << 3,
    SubstitutionOfCreditOrLiquidityProvider = 1 << 4,
    AdverseTaxOpinionOrEvent = 1 << 5,
    ModificationsToRightsOfSecurityHolders = 1 << 6,
    BondCallOrTenderOffer = 1 << 7,
    Defeasance = 1 << 8,
    ReleaseSubstitutionOrSaleOfProperty = 1 << 9,
    RatingChange = 1 << 10,
    BankruptcyInsolvencyReceivership = 1 << 11,
    MergerAcquisitionOrAssetSale = 1 << 12,
    SuccessorOrAdditionalTrusteeAppointment = 1 << 13,
    FinancialObligationDefaultOrModification = 1 << 14
};

enum class MuniExemptionStatus : uint8_t {
    NonExempt = 0,             // Full Rule 15c2-12 compliance required (Offering >= $1M)
    SmallOfferingExempt = 1,   // Offering aggregate principal < $1,000,000
    AuthorizedDenominationsExempt = 2, // $100k+ denominations sold to <= 35 sophisticated investors or <= 9 months maturity
    CommercialPaperExempt = 3  // Maturity <= 270 days in authorized denominations
};

struct MuniOfferingRecord {
    char cusip_prefix[8]{0};       // 6-digit issuer CUSIP prefix
    char issuer_name[64]{0};
    double aggregate_principal{0.0};
    uint32_t min_denomination{0};
    uint32_t maturity_days{0};
    MuniExemptionStatus exemption{MuniExemptionStatus::NonExempt};
    bool pos_deemed_final{false};  // Preliminary Official Statement deemed final by issuer
    bool final_os_received{false}; // Final Official Statement received within 7 business days
    uint64_t pos_timestamp_ns{0};
    uint64_t final_os_timestamp_ns{0};
    bool cdsa_executed{false};     // Continuing Disclosure Service Agreement in place
    uint16_t active_event_flags{0}; // Bitfield of MuniMaterialEventType
    bool emma_filing_verified{false};
};

struct MuniTradeValidationResult {
    bool trade_allowed{false};
    bool exempt{false};
    bool disclosure_deficiency{false};
    bool material_event_warning{false};
    char violation_reason[64]{0};
};

class SEC15c212Validator {
public:
    static constexpr size_t MAX_OFFERINGS = 128;
    static constexpr uint64_t SEVEN_BUSINESS_DAYS_NS = 7ULL * 24 * 3600 * 1'000'000'000ULL;

    SEC15c212Validator() noexcept {
        reset();
    }

    void reset() noexcept {
        offering_count_ = 0;
    }

    bool register_offering(const MuniOfferingRecord& record) noexcept {
        if (offering_count_ >= MAX_OFFERINGS) return false;
        offerings_[offering_count_++] = record;
        return true;
    }

    MuniTradeValidationResult validate_trade(const char* cusip_prefix, uint64_t trade_time_ns, double trade_amount) const noexcept {
        (void)trade_time_ns;
        (void)trade_amount;
        MuniTradeValidationResult result{};

        const MuniOfferingRecord* rec = nullptr;
        for (size_t i = 0; i < offering_count_; ++i) {
            if (std::strncmp(offerings_[i].cusip_prefix, cusip_prefix, 6) == 0) {
                rec = &offerings_[i];
                break;
            }
        }

        if (!rec) {
            // Unregistered issuer
            result.trade_allowed = false;
            std::strncpy(result.violation_reason, "MUNI_ISSUER_NOT_REGISTERED", sizeof(result.violation_reason) - 1);
            return result;
        }

        // Check statutory exemptions under Rule 15c2-12(d)
        if (rec->aggregate_principal < 1'000'000.0) {
            result.trade_allowed = true;
            result.exempt = true;
            return result;
        }

        if (rec->exemption == MuniExemptionStatus::AuthorizedDenominationsExempt ||
            rec->exemption == MuniExemptionStatus::CommercialPaperExempt) {
            result.trade_allowed = true;
            result.exempt = true;
            return result;
        }

        // Rule 15c2-12(b)(1): Broker-dealer must obtain POS deemed final prior to bid/purchase
        if (!rec->pos_deemed_final) {
            result.trade_allowed = false;
            result.disclosure_deficiency = true;
            std::strncpy(result.violation_reason, "POS_NOT_DEEMED_FINAL", sizeof(result.violation_reason) - 1);
            return result;
        }

        // Rule 15c2-12(b)(3): Final Official Statement must be delivered within 7 business days
        if (rec->final_os_timestamp_ns > 0 && rec->pos_timestamp_ns > 0) {
            if ((rec->final_os_timestamp_ns - rec->pos_timestamp_ns) > SEVEN_BUSINESS_DAYS_NS && !rec->final_os_received) {
                result.trade_allowed = false;
                result.disclosure_deficiency = true;
                std::strncpy(result.violation_reason, "FINAL_OS_DELIVERY_BREACH", sizeof(result.violation_reason) - 1);
                return result;
            }
        }

        // Rule 15c2-12(b)(5): CDSA (Continuing Disclosure Agreement) mandatory
        if (!rec->cdsa_executed) {
            result.trade_allowed = false;
            result.disclosure_deficiency = true;
            std::strncpy(result.violation_reason, "CDSA_AGREEMENT_MISSING", sizeof(result.violation_reason) - 1);
            return result;
        }

        // Material Event Check
        if (rec->active_event_flags != 0) {
            result.material_event_warning = true;
            // Delinquency, Non-Payment Default, or Bankruptcy strictly halts primary trading
            uint16_t severe_mask = static_cast<uint16_t>(MuniMaterialEventType::PrincipalAndInterestPaymentDelinquency) |
                                   static_cast<uint16_t>(MuniMaterialEventType::NonPaymentRelatedDefault) |
                                   static_cast<uint16_t>(MuniMaterialEventType::BankruptcyInsolvencyReceivership);
            if ((rec->active_event_flags & severe_mask) != 0 && !rec->emma_filing_verified) {
                result.trade_allowed = false;
                std::strncpy(result.violation_reason, "UNRESOLVED_SEVERE_MATERIAL_DEFAULT", sizeof(result.violation_reason) - 1);
                return result;
            }
        }

        result.trade_allowed = true;
        return result;
    }

    size_t offering_count() const noexcept { return offering_count_; }

private:
    std::array<MuniOfferingRecord, MAX_OFFERINGS> offerings_{};
    size_t offering_count_{0};
};

} // namespace luv
