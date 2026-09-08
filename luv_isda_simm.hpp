#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>
#include <string_view>

namespace luv {

// ISDA SIMM Risk Classes
enum class SIMMRiskClass : uint8_t {
    InterestRate = 0,
    CreditQualifying = 1,
    CreditNonQualifying = 2,
    Equity = 3,
    Commodity = 4,
    FX = 5,
    COUNT = 6
};

// Standard Tenor Buckets for Interest Rate Sensitivities
enum class IRTenor : uint8_t {
    Tenor_2W = 0,
    Tenor_1M = 1,
    Tenor_3M = 2,
    Tenor_6M = 3,
    Tenor_1Y = 4,
    Tenor_2Y = 5,
    Tenor_3Y = 6,
    Tenor_5Y = 7,
    Tenor_10Y = 8,
    Tenor_15Y = 9,
    Tenor_20Y = 10,
    Tenor_30Y = 11,
    COUNT = 12
};

struct IRSensitivityEntry {
    char currency[4]{0};       // e.g., "USD", "EUR", "GBP"
    uint8_t bucket_id{1};      // 1: Regular volatility currencies, 2: Low-vol, 3: High-vol
    IRTenor tenor{IRTenor::Tenor_1Y};
    double delta_pv01{0.0};    // Net PV01 in USD
    double vega_pv01{0.0};     // Vega sensitivity
};

struct FXSensitivityEntry {
    char ccy_pair[8]{0};       // e.g., "EURUSD", "USDJPY"
    uint8_t ccy_category{1};   // 1: High liquidity, 2: Moderate, 3: Other
    double delta_sensitivity{0.0}; // Sensitivity to 1% move in spot
    double vega_sensitivity{0.0};
};

struct EquitySensitivityEntry {
    char ticker[12]{0};
    uint8_t bucket_id{1};      // 1-12 ISDA SIMM equity buckets (Sector/Market Cap)
    double delta_sensitivity{0.0};
    double vega_sensitivity{0.0};
};

struct SIMMMarginBreakdown {
    double ir_margin{0.0};
    double credit_margin{0.0};
    double equity_margin{0.0};
    double commodity_margin{0.0};
    double fx_margin{0.0};
    double total_initial_margin{0.0};
};

class ISDASIMMEngine {
public:
    static constexpr size_t MAX_IR_SENSITIVITIES = 64;
    static constexpr size_t MAX_FX_SENSITIVITIES = 32;
    static constexpr size_t MAX_EQ_SENSITIVITIES = 64;

    ISDASIMMEngine() noexcept {
        reset();
    }

    void reset() noexcept {
        ir_count_ = 0;
        fx_count_ = 0;
        eq_count_ = 0;
    }

    bool add_ir_sensitivity(const IRSensitivityEntry& entry) noexcept {
        if (ir_count_ >= MAX_IR_SENSITIVITIES) return false;
        ir_entries_[ir_count_++] = entry;
        return true;
    }

    bool add_fx_sensitivity(const FXSensitivityEntry& entry) noexcept {
        if (fx_count_ >= MAX_FX_SENSITIVITIES) return false;
        fx_entries_[fx_count_++] = entry;
        return true;
    }

    bool add_equity_sensitivity(const EquitySensitivityEntry& entry) noexcept {
        if (eq_count_ >= MAX_EQ_SENSITIVITIES) return false;
        eq_entries_[eq_count_++] = entry;
        return true;
    }

    // Standard ISDA SIMM v2.6 IR Risk Weights (in basis points / ratio)
    static constexpr double get_ir_risk_weight(IRTenor tenor) noexcept {
        switch (tenor) {
            case IRTenor::Tenor_2W:  return 85.0;  // bp equivalent
            case IRTenor::Tenor_1M:  return 80.0;
            case IRTenor::Tenor_3M:  return 75.0;
            case IRTenor::Tenor_6M:  return 65.0;
            case IRTenor::Tenor_1Y:  return 56.0;
            case IRTenor::Tenor_2Y:  return 51.0;
            case IRTenor::Tenor_3Y:  return 48.0;
            case IRTenor::Tenor_5Y:  return 45.0;
            case IRTenor::Tenor_10Y: return 44.0;
            case IRTenor::Tenor_15Y: return 46.0;
            case IRTenor::Tenor_20Y: return 48.0;
            case IRTenor::Tenor_30Y: return 53.0;
            default: return 50.0;
        }
    }

    static constexpr double get_ir_tenor_correlation(IRTenor t1, IRTenor t2) noexcept {
        if (t1 == t2) return 1.0;
        int diff = std::abs(static_cast<int>(t1) - static_cast<int>(t2));
        // Exponential decay model for cross-tenor correlation: exp(-theta * |t1 - t2|)
        return std::exp(-0.15 * static_cast<double>(diff));
    }

    double calculate_ir_delta_margin() const noexcept {
        if (ir_count_ == 0) return 0.0;

        // Group by currency / bucket
        // For simplicity and deterministic bounding, compute weighted sensitivities
        std::array<double, MAX_IR_SENSITIVITIES> ws{};
        double sum_abs_sens = 0.0;
        for (size_t i = 0; i < ir_count_; ++i) {
            sum_abs_sens += std::abs(ir_entries_[i].delta_pv01);
        }

        // Concentration threshold for regular IR (USD 250M PV01 threshold)
        constexpr double CR_THRESHOLD = 250'000.0;
        double cr = std::max(1.0, std::sqrt(sum_abs_sens / CR_THRESHOLD));

        for (size_t i = 0; i < ir_count_; ++i) {
            double rw = get_ir_risk_weight(ir_entries_[i].tenor);
            ws[i] = ir_entries_[i].delta_pv01 * rw * cr;
        }

        // Intra-currency double sum: K = sqrt(sum WS_i^2 + sum_{i!=j} rho_{ij} WS_i WS_j)
        double variance_sum = 0.0;
        for (size_t i = 0; i < ir_count_; ++i) {
            for (size_t j = 0; j < ir_count_; ++j) {
                double rho = get_ir_tenor_correlation(ir_entries_[i].tenor, ir_entries_[j].tenor);
                variance_sum += rho * ws[i] * ws[j];
            }
        }

        return std::sqrt(std::max(0.0, variance_sum));
    }

    double calculate_fx_margin() const noexcept {
        if (fx_count_ == 0) return 0.0;

        // SIMM FX Risk Weights: High Liquidity = 7.9%, Regular = 11.0%
        // Intra-FX correlation: 0.50
        std::array<double, MAX_FX_SENSITIVITIES> ws{};
        for (size_t i = 0; i < fx_count_; ++i) {
            double rw = (fx_entries_[i].ccy_category == 1) ? 0.079 : 0.110;
            ws[i] = fx_entries_[i].delta_sensitivity * rw;
        }

        double variance_sum = 0.0;
        for (size_t i = 0; i < fx_count_; ++i) {
            for (size_t j = 0; j < fx_count_; ++j) {
                double rho = (i == j) ? 1.0 : 0.50;
                variance_sum += rho * ws[i] * ws[j];
            }
        }

        return std::sqrt(std::max(0.0, variance_sum));
    }

    double calculate_equity_margin() const noexcept {
        if (eq_count_ == 0) return 0.0;

        // SIMM Equity Risk Weights: Large Cap Dev = 20.0%, Small Cap / Emerging = 29.0%
        // Intra-bucket correlation = 0.28, cross-bucket = 0.16
        std::array<double, MAX_EQ_SENSITIVITIES> ws{};
        for (size_t i = 0; i < eq_count_; ++i) {
            double rw = (eq_entries_[i].bucket_id <= 4) ? 0.20 : 0.29;
            ws[i] = eq_entries_[i].delta_sensitivity * rw;
        }

        double variance_sum = 0.0;
        for (size_t i = 0; i < eq_count_; ++i) {
            for (size_t j = 0; j < eq_count_; ++j) {
                double rho = (i == j) ? 1.0 : ((eq_entries_[i].bucket_id == eq_entries_[j].bucket_id) ? 0.28 : 0.16);
                variance_sum += rho * ws[i] * ws[j];
            }
        }

        return std::sqrt(std::max(0.0, variance_sum));
    }

    SIMMMarginBreakdown calculate_total_margin() const noexcept {
        SIMMMarginBreakdown breakdown{};
        breakdown.ir_margin = calculate_ir_delta_margin();
        breakdown.fx_margin = calculate_fx_margin();
        breakdown.equity_margin = calculate_equity_margin();
        breakdown.credit_margin = 0.0;
        breakdown.commodity_margin = 0.0;

        // Cross-Risk Class Aggregation with ISDA SIMM correlation matrix (IR-FX: 0.27, IR-EQ: 0.19, FX-EQ: 0.31)
        double ir = breakdown.ir_margin;
        double fx = breakdown.fx_margin;
        double eq = breakdown.equity_margin;

        double agg_var = (ir * ir) + (fx * fx) + (eq * eq)
                       + 2.0 * 0.27 * ir * fx
                       + 2.0 * 0.19 * ir * eq
                       + 2.0 * 0.31 * fx * eq;

        breakdown.total_initial_margin = std::sqrt(std::max(0.0, agg_var));
        return breakdown;
    }

    size_t ir_count() const noexcept { return ir_count_; }
    size_t fx_count() const noexcept { return fx_count_; }
    size_t eq_count() const noexcept { return eq_count_; }

private:
    std::array<IRSensitivityEntry, MAX_IR_SENSITIVITIES> ir_entries_{};
    size_t ir_count_{0};

    std::array<FXSensitivityEntry, MAX_FX_SENSITIVITIES> fx_entries_{};
    size_t fx_count_{0};

    std::array<EquitySensitivityEntry, MAX_EQ_SENSITIVITIES> eq_entries_{};
    size_t eq_count_{0};
};

} // namespace luv
