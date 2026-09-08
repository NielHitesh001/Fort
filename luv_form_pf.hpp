#pragma once

#include <cstdint>
#include <array>
#include <algorithm>

namespace luv {
namespace compliance {

enum class FormPfTier : uint8_t {
    kSmallPrivateFund = 0,
    kLargeHedgeFund = 1,        // RAUM >= $1.5B
    kLargeLiquidityFund = 2,    // RAUM >= $1B
    kLargePrivateEquity = 3     // RAUM >= $2B
};

struct AssetClassExposure {
    uint16_t asset_class_id = 0; // 1: Equities, 2: Fixed Income, 3: Derivatives, 4: FX
    int64_t gross_long_notional = 0;
    int64_t gross_short_notional = 0;
};

struct FormPfReport {
    int64_t net_asset_value = 0;
    int64_t gross_notional_exposure = 0;
    double gross_leverage_ratio = 0.0;
    FormPfTier tier = FormPfTier::kSmallPrivateFund;
    bool requires_quarterly_filing = true;
};

class FormPfReporter {
public:
    static constexpr size_t kMaxAssetClasses = 16;
    static constexpr int64_t kLargeHedgeFundThreshold = 1'500'000'000'0000LL; // $1.5B (x10,000)

    FormPfReporter() noexcept : num_classes_(0) {}

    bool record_exposure(uint16_t asset_class_id, int64_t long_notional, int64_t short_notional) noexcept {
        if (num_classes_ >= kMaxAssetClasses) return false;
        exposures_[num_classes_++] = AssetClassExposure{asset_class_id, long_notional, short_notional};
        return true;
    }

    FormPfReport generate_report(int64_t net_asset_value) const noexcept {
        FormPfReport report{};
        report.net_asset_value = net_asset_value;

        int64_t total_gne = 0;
        for (size_t i = 0; i < num_classes_; ++i) {
            total_gne += (exposures_[i].gross_long_notional + exposures_[i].gross_short_notional);
        }

        report.gross_notional_exposure = total_gne;

        if (net_asset_value > 0) {
            report.gross_leverage_ratio = static_cast<double>(total_gne) / static_cast<double>(net_asset_value);
        }

        if (net_asset_value >= kLargeHedgeFundThreshold) {
            report.tier = FormPfTier::kLargeHedgeFund;
            report.requires_quarterly_filing = true;
        } else {
            report.tier = FormPfTier::kSmallPrivateFund;
            report.requires_quarterly_filing = false; // Annual filing for small funds
        }

        return report;
    }

private:
    std::array<AssetClassExposure, kMaxAssetClasses> exposures_{};
    size_t num_classes_{0};
};

} // namespace compliance
} // namespace luv
