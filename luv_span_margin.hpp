#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace risk {

// 16 Standard SPAN Risk Scenarios (Price Shock [-3 to +3 sigma] x Vol Shock [-/+ delta vol])
struct SpanScenario {
    double price_shock_pct = 0.0; // e.g. -0.15 to +0.15
    double vol_shock_pct = 0.0;   // e.g. -0.20 to +0.20
};

struct SpanContractRiskArray {
    uint64_t contract_id = 0;
    std::array<int64_t, 16> loss_matrix{}; // Dollar loss per scenario for 1 contract
};

struct PortfolioPosition {
    uint64_t contract_id = 0;
    int64_t net_qty = 0; // positive for long, negative for short
};

class SpanMarginEngine {
public:
    static constexpr size_t kMaxContracts = 64;
    static constexpr size_t kMaxPositions = 64;

    SpanMarginEngine() noexcept : num_contracts_(0), num_positions_(0) {}

    bool register_contract_span_array(const SpanContractRiskArray& array) noexcept {
        if (num_contracts_ >= kMaxContracts) return false;
        contracts_[num_contracts_++] = array;
        return true;
    }

    bool set_position(uint64_t contract_id, int64_t qty) noexcept {
        for (size_t i = 0; i < num_positions_; ++i) {
            if (positions_[i].contract_id == contract_id) {
                positions_[i].net_qty = qty;
                return true;
            }
        }
        if (num_positions_ >= kMaxPositions) return false;
        positions_[num_positions_++] = PortfolioPosition{contract_id, qty};
        return true;
    }

    // Evaluates the maximum portfolio loss across all 16 SPAN scenarios (Scanning Risk Requirement)
    int64_t compute_span_scanning_requirement() const noexcept {
        if (num_positions_ == 0 || num_contracts_ == 0) return 0;

        std::array<int64_t, 16> scenario_portfolio_losses{};

        for (size_t p = 0; p < num_positions_; ++p) {
            const auto& pos = positions_[p];
            if (pos.net_qty == 0) continue;

            const SpanContractRiskArray* risk_arr = nullptr;
            for (size_t c = 0; c < num_contracts_; ++c) {
                if (contracts_[c].contract_id == pos.contract_id) {
                    risk_arr = &contracts_[c];
                    break;
                }
            }

            if (!risk_arr) continue;

            for (size_t s = 0; s < 16; ++s) {
                scenario_portfolio_losses[s] += risk_arr->loss_matrix[s] * pos.net_qty;
            }
        }

        // SPAN Scanning Risk Requirement is the worst-case scenario loss
        int64_t max_loss = 0;
        for (size_t s = 0; s < 16; ++s) {
            if (scenario_portfolio_losses[s] > max_loss) {
                max_loss = scenario_portfolio_losses[s];
            }
        }

        return max_loss;
    }

private:
    std::array<SpanContractRiskArray, kMaxContracts> contracts_{};
    size_t num_contracts_{0};
    std::array<PortfolioPosition, kMaxPositions> positions_{};
    size_t num_positions_{0};
};

} // namespace risk
} // namespace luv
