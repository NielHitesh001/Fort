#include <iostream>
#include <cassert>
#include <cmath>
#include "luv_crypto_basis_arbitrage.hpp"

int main() {
    std::cout << "[TEST] Running Crypto Cash-and-Carry Basis Arbitrage Engine Test...\n";

    // Scenario 1: Positive Funding (Perp at premium, funding +0.03% / 8h = 32.85% APR)
    luv::BasisCarryParams p1;
    p1.spot_price = 50000.0;
    p1.perp_price = 50100.0;
    p1.eight_hour_funding_rate = 0.0003; // +3 bps / 8h
    p1.taker_fee_pct = 0.0005;          // 5 bps

    auto res1 = luv::CryptoBasisArbitrageEngine::evaluate_carry(p1);
    assert(res1.is_carry_profitable);
    assert(res1.annualized_funding_apr > 0.32 && res1.annualized_funding_apr < 0.33); // ~32.85%
    assert(res1.net_annualized_apy > 0.30); // ~32.65% after fees

    // Scenario 2: Negligible funding (+0.001% / 8h = 1.09% APR < 5% hurdle)
    luv::BasisCarryParams p2;
    p2.spot_price = 50000.0;
    p2.perp_price = 50005.0;
    p2.eight_hour_funding_rate = 0.00001;
    p2.taker_fee_pct = 0.0005;

    auto res2 = luv::CryptoBasisArbitrageEngine::evaluate_carry(p2);
    assert(!res2.is_carry_profitable);
    assert(res2.net_annualized_apy < 0.02);

    std::cout << "[TEST] High Funding Net APY: " << (res1.net_annualized_apy * 100.0) << "%\n";
    std::cout << "[TEST] Low Funding Net APY: " << (res2.net_annualized_apy * 100.0) << "%\n";
    std::cout << "[TEST] Crypto Cash-and-Carry Basis Arbitrage Engine Test Passed!\n";
    return 0;
}
