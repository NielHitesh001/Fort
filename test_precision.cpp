#include "luv_precision.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_equity_precision() {
    luv::InstrumentMetadata meta;
    meta.symbol_idx = 1;
    meta.asset_class = luv::AssetClass::kEquity;
    meta.price_decimals = 2; // Cents
    meta.qty_decimals = 0;   // Shares
    meta.min_order_qty = 1;
    meta.max_order_qty = 100'000;
    meta.tick_size = 1;      // $0.01
    meta.min_notional = 100; // $1.00

    int64_t scaled_price = luv::PrecisionConverter::to_scaled_price(150.25, meta.price_decimals);
    assert(scaled_price == 15025);
    double price_back = luv::PrecisionConverter::from_scaled_price(scaled_price, meta.price_decimals);
    assert(std::abs(price_back - 150.25) < 1e-6);

    const char* err = nullptr;
    assert(luv::PrecisionConverter::validate_order_bounds(meta, scaled_price, 100, &err));

    // Test below min qty
    assert(!luv::PrecisionConverter::validate_order_bounds(meta, scaled_price, 0, &err));
    std::printf("[PASS] test_equity_precision\n");
}

void test_fx_pip_precision() {
    luv::InstrumentMetadata meta;
    meta.symbol_idx = 2;
    meta.asset_class = luv::AssetClass::kForeignExchange;
    meta.price_decimals = 5; // EUR/USD pip (0.00001)
    meta.qty_decimals = 0;   // Lots
    meta.min_order_qty = 1000;
    meta.max_order_qty = 10'000'000;
    meta.tick_size = 5;      // 0.5 pip
    meta.min_notional = 1000;

    int64_t scaled_price = luv::PrecisionConverter::to_scaled_price(1.08545, meta.price_decimals);
    assert(scaled_price == 108545);

    const char* err = nullptr;
    assert(luv::PrecisionConverter::validate_order_bounds(meta, scaled_price, 10000, &err));

    // Violate tick size (not divisible by 5)
    assert(!luv::PrecisionConverter::validate_order_bounds(meta, 108543, 10000, &err));
    std::printf("[PASS] test_fx_pip_precision\n");
}

void test_crypto_fractional_precision() {
    luv::InstrumentMetadata meta;
    meta.symbol_idx = 3;
    meta.asset_class = luv::AssetClass::kCryptocurrency;
    meta.price_decimals = 2; // USD cents
    meta.qty_decimals = 8;   // Satoshis
    meta.min_order_qty = 1000; // 0.00001000 BTC
    meta.tick_size = 1;
    meta.min_notional = 100; // $1.00

    int64_t scaled_qty = luv::PrecisionConverter::to_scaled_qty(0.00500000, meta.qty_decimals);
    assert(scaled_qty == 500000);
    double qty_back = luv::PrecisionConverter::from_scaled_qty(scaled_qty, meta.qty_decimals);
    assert(std::abs(qty_back - 0.005) < 1e-9);

    int64_t scaled_price = luv::PrecisionConverter::to_scaled_price(65000.50, meta.price_decimals);
    assert(scaled_price == 6500050);

    const char* err = nullptr;
    assert(luv::PrecisionConverter::validate_order_bounds(meta, scaled_price, scaled_qty, &err));
    std::printf("[PASS] test_crypto_fractional_precision\n");
}

int main() {
    test_equity_precision();
    test_fx_pip_precision();
    test_crypto_fractional_precision();
    std::printf("All multi-asset precision tests passed successfully.\n");
    return 0;
}
