#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace luv {
namespace collateral {

enum class CollateralType : uint8_t {
    kCashUSD = 0,
    kCashEUR = 1,
    kUsTreasury = 2,
    kEquitiesTier1 = 3,
    kEquitiesTier2 = 4,
    kCryptoBase = 5
};

struct CollateralAsset {
    CollateralType type = CollateralType::kCashUSD;
    int64_t raw_units = 0;       // Quantity/balance in scaled integer units
    double market_price_usd = 1.0;
    double haircut_pct = 0.0;    // e.g. 0.0% for USD, 2.0% for Treasuries, 20.0% for Equities
    bool active = false;
};

class CollateralManagementEngine {
public:
    static constexpr size_t kMaxAssets = 32;

    CollateralManagementEngine() noexcept : num_assets_(0) {}

    bool deposit_asset(
        CollateralType type,
        int64_t units,
        double market_price_usd,
        double haircut_pct) noexcept
    {
        for (size_t i = 0; i < num_assets_; ++i) {
            if (assets_[i].active && assets_[i].type == type) {
                assets_[i].raw_units += units;
                assets_[i].market_price_usd = market_price_usd;
                assets_[i].haircut_pct = haircut_pct;
                return true;
            }
        }

        if (num_assets_ < kMaxAssets) {
            assets_[num_assets_++] = CollateralAsset{
                .type = type,
                .raw_units = units,
                .market_price_usd = market_price_usd,
                .haircut_pct = haircut_pct,
                .active = true
            };
            return true;
        }
        return false;
    }

    bool withdraw_asset(CollateralType type, int64_t units) noexcept {
        for (size_t i = 0; i < num_assets_; ++i) {
            if (assets_[i].active && assets_[i].type == type) {
                if (assets_[i].raw_units >= units) {
                    assets_[i].raw_units -= units;
                    return true;
                }
                return false; // Insufficient units
            }
        }
        return false;
    }

    // Computes total collateral value after haircuts in USD cents/units
    int64_t compute_effective_collateral_usd() const noexcept {
        double total_usd = 0.0;

        for (size_t i = 0; i < num_assets_; ++i) {
            if (!assets_[i].active || assets_[i].raw_units <= 0) continue;

            double nominal_val = static_cast<double>(assets_[i].raw_units) * assets_[i].market_price_usd;
            double post_haircut_val = nominal_val * (1.0 - (assets_[i].haircut_pct / 100.0));
            total_usd += post_haircut_val;
        }

        return static_cast<int64_t>(std::round(total_usd));
    }

    // Standard regulatory haircuts (Basel III / CFTC Part 39)
    static double get_standard_haircut_pct(CollateralType type) noexcept {
        switch (type) {
            case CollateralType::kCashUSD: return 0.0;      // 0%
            case CollateralType::kCashEUR: return 2.0;      // 2% FX risk
            case CollateralType::kUsTreasury: return 2.0;   // 2% sovereign risk
            case CollateralType::kEquitiesTier1: return 15.0; // 15% large-cap
            case CollateralType::kEquitiesTier2: return 30.0; // 30% mid/small-cap
            case CollateralType::kCryptoBase: return 50.0;   // 50% crypto haircut
            default: return 100.0;
        }
    }

private:
    std::array<CollateralAsset, kMaxAssets> assets_{};
    size_t num_assets_{0};
};

} // namespace collateral
} // namespace luv
