# Fort Code Examples & Tutorial

This document provides complete, working C++20 code examples demonstrating core workflows.

---

## Example 1: Creating an Order Book & Adding Limit Orders

```cpp
#include "luv_arena.hpp"
#include "luv_lob.hpp"
#include <iostream>

int main() {
    // 1. Initialize pre-allocated memory arena (64 MB)
    luv::Arena arena;
    if (!arena.init(64 * 1024 * 1024)) {
        std::cerr << "Failed to allocate memory arena.\n";
        return 1;
    }

    // 2. Initialize Limit Order Book
    luv::LimitOrderBook lob;
    if (!lob.init(arena)) {
        std::cerr << "Failed to initialize LOB.\n";
        return 1;
    }

    // 3. Add Buy Order: 100 shares @ $150.00
    luv::Order buy_order{
        .order_id = 1001,
        .side = luv::exec::kBuy,
        .price = 15000, // 150.00 USD (in cents)
        .qty = 100,
        .timestamp_ns = 1'000'000'000ULL
    };
    lob.insert_order(buy_order);

    // 4. Query Best Bid
    auto best_bid = lob.best_bid();
    std::cout << "Best Bid: $" << (best_bid.price / 100.0) 
              << " x " << best_bid.quantity << " shares\n";

    return 0;
}
```

---

## Example 2: Pricing Stochastic Volatility Options with Bates (1996)

```cpp
#include "luv_bates_pricer.hpp"
#include <iostream>

int main() {
    luv::BatesParameters params{
        .spot_price = 100.0,
        .strike_price = 100.0,
        .risk_free_rate = 0.05,
        .dividend_yield = 0.01,
        .time_to_maturity = 1.0,
        .initial_variance = 0.04,        // 20% vol
        .mean_reversion_kappa = 2.0,
        .long_term_var_theta = 0.04,
        .vol_of_vol_xi = 0.25,
        .correlation_rho = -0.60,
        .jump_intensity_lambda = 0.20,   // 0.2 jumps per year
        .jump_mean_gamma = -0.10,        // -10% mean jump size
        .jump_vol_delta = 0.15           // 15% jump size volatility
    };

    auto result = luv::BatesOptionPricer::price_european_option(params);
    if (result.valid) {
        std::cout << "Bates Call Price: $" << result.call_price << "\n";
        std::cout << "Bates Put Price:  $" << result.put_price << "\n";
        std::cout << "Call Delta:       " << result.greeks.delta << "\n";
        std::cout << "Call Gamma:       " << result.greeks.gamma << "\n";
        std::cout << "Call Vega:        " << result.greeks.vega << "\n";
    }

    return 0;
}
```

---

## Example 3: Live LOB Feature Extraction & Decision Tree Inference

```cpp
#include "luv_tree_feature_pipeline.hpp"
#include <iostream>

int main() {
    luv::TreeFeaturePipelineEngine engine;

    // Simulate incoming order book depth update
    engine.on_depth_update(
        150'00, 1000,   // Best Bid: $150.00 x 1000
        150'05, 200,    // Best Ask: $150.05 x 200
        5000, 1200      // L1-L5 Depth: 5000 Bid vs 1200 Ask
    );

    // Record incoming trade tick
    engine.on_trade(true /* buyer-initiated */, 300);

    // Predict price directional alpha
    auto prediction = engine.predict();
    if (prediction.valid) {
        std::cout << "Directional Alpha:        " << prediction.directional_alpha << "\n";
        std::cout << "Adverse Selection Prob:  " << prediction.adverse_selection_prob << "\n";
        std::cout << "Execution Urgency:       " << prediction.execution_urgency << "\n";
    }

    return 0;
}
```
