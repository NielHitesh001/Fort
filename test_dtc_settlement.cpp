#include "luv_dtc_settlement.hpp"
#include <cassert>
#include <cstdio>

void test_dtc_settlement_matching_and_ftd() {
    luv::clearing::DtcSettlementEngine engine;

    // 1. Matched Instruction: Delivering Participant 101 -> Receiving 202, 1000 shares @ $150.00
    assert(engine.record_deliver_instruction(luv::clearing::DtcDeliveryInstruction{
        .instruction_id = 1,
        .delivering_participant_id = 101,
        .receiving_participant_id = 202,
        .cusip_symbol_idx = 1,
        .share_qty = 1000,
        .settlement_amount_cents = 15000000,
        .settlement_date_ns = 2'000'000'000ULL,
        .affirmed = false
    }));
    assert(engine.record_receive_instruction(luv::clearing::DtcDeliveryInstruction{
        .instruction_id = 101,
        .delivering_participant_id = 101,
        .receiving_participant_id = 202,
        .cusip_symbol_idx = 1,
        .share_qty = 1000,
        .settlement_amount_cents = 15000000,
        .settlement_date_ns = 2'000'000'000ULL,
        .affirmed = false
    }));

    // 2. Failed to Deliver (FTD) Instruction: Delivering Participant 303 has no matching receipt past settlement date
    assert(engine.record_deliver_instruction(luv::clearing::DtcDeliveryInstruction{
        .instruction_id = 2,
        .delivering_participant_id = 303,
        .receiving_participant_id = 404,
        .cusip_symbol_idx = 2,
        .share_qty = 500,
        .settlement_amount_cents = 5000000,
        .settlement_date_ns = 1'000'000'000ULL,
        .affirmed = false
    }));

    std::array<luv::clearing::DtcSettlementMatchResult, 4> matches{};
    size_t count = engine.match_settlements(3'000'000'000ULL, matches.data(), matches.size());

    assert(count == 2);
    assert(matches[0].instruction_id == 1 && matches[0].status == luv::clearing::DtcMatchStatus::kAffirmedMatched);
    assert(matches[1].instruction_id == 2 && matches[1].status == luv::clearing::DtcMatchStatus::kFailedToDeliver);
    assert(matches[1].buy_in_notice_required == true);

    std::printf("[PASS] test_dtc_settlement_matching_and_ftd (Matched 1 instruction, FTD identified on instruction 2)\n");
}

int main() {
    test_dtc_settlement_matching_and_ftd();
    std::printf("All DTC settlement matching tests passed successfully.\n");
    return 0;
}
