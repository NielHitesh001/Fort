#include "luv_rts28.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting MiFID II RTS 28 Venue Execution Reporter Tests..." << std::endl;

    luv::RTS28Reporter reporter;

    // Simulate executions across 6 venues for Equities (Professional Clients)
    // Venues: XNAS, XNYS, BATS, XCBO, IEXG, EDGA
    // 1. XNAS - 1,000,000 USD (500 orders, 300 passive, 200 aggressive)
    for (int i = 0; i < 500; ++i) {
        luv::ExecutionRecord rec{};
        rec.order_id = 1000 + i;
        rec.inst_class = luv::InstrumentClass::Equities_SharesAndDepositaryReceipts;
        rec.client_cat = luv::ClientCategory::Professional;
        std::strncpy(rec.venue_mic, "XNAS", 4);
        std::strncpy(rec.venue_lei, "54930006W8G77L6G1O47", 20);
        rec.gross_notional = 2000.0;
        rec.is_passive = (i < 300);
        rec.is_aggressive = (i >= 300);
        rec.is_directed = (i % 10 == 0); // 10% directed
        assert(reporter.record_execution(rec));
    }

    // 2. XNYS - 800,000 USD (400 orders)
    for (int i = 0; i < 400; ++i) {
        luv::ExecutionRecord rec{};
        rec.order_id = 2000 + i;
        rec.inst_class = luv::InstrumentClass::Equities_SharesAndDepositaryReceipts;
        rec.client_cat = luv::ClientCategory::Professional;
        std::strncpy(rec.venue_mic, "XNYS", 4);
        std::strncpy(rec.venue_lei, "549300E4R4V0M6A61122", 20);
        rec.gross_notional = 2000.0;
        rec.is_passive = (i < 200);
        rec.is_aggressive = (i >= 200);
        rec.is_directed = false;
        assert(reporter.record_execution(rec));
    }

    // 3. BATS - 400,000 USD (200 orders)
    for (int i = 0; i < 200; ++i) {
        luv::ExecutionRecord rec{};
        rec.order_id = 3000 + i;
        rec.inst_class = luv::InstrumentClass::Equities_SharesAndDepositaryReceipts;
        rec.client_cat = luv::ClientCategory::Professional;
        std::strncpy(rec.venue_mic, "BATS", 4);
        std::strncpy(rec.venue_lei, "549300BBBBBBBBBBBBBB", 20);
        rec.gross_notional = 2000.0;
        rec.is_passive = (i < 150);
        rec.is_aggressive = (i >= 150);
        assert(reporter.record_execution(rec));
    }

    // 4. XCBO - 200,000 USD (100 orders)
    for (int i = 0; i < 100; ++i) {
        luv::ExecutionRecord rec{};
        rec.order_id = 4000 + i;
        rec.inst_class = luv::InstrumentClass::Equities_SharesAndDepositaryReceipts;
        rec.client_cat = luv::ClientCategory::Professional;
        std::strncpy(rec.venue_mic, "XCBO", 4);
        std::strncpy(rec.venue_lei, "549300CCCCCCCCCCCCCC", 20);
        rec.gross_notional = 2000.0;
        assert(reporter.record_execution(rec));
    }

    // 5. IEXG - 100,000 USD (50 orders)
    for (int i = 0; i < 50; ++i) {
        luv::ExecutionRecord rec{};
        rec.order_id = 5000 + i;
        rec.inst_class = luv::InstrumentClass::Equities_SharesAndDepositaryReceipts;
        rec.client_cat = luv::ClientCategory::Professional;
        std::strncpy(rec.venue_mic, "IEXG", 4);
        std::strncpy(rec.venue_lei, "549300DDDDDDDDDDDDDD", 20);
        rec.gross_notional = 2000.0;
        assert(reporter.record_execution(rec));
    }

    // 6. EDGA - 50,000 USD (25 orders) -> Should be ranked 6th and excluded from top 5
    for (int i = 0; i < 25; ++i) {
        luv::ExecutionRecord rec{};
        rec.order_id = 6000 + i;
        rec.inst_class = luv::InstrumentClass::Equities_SharesAndDepositaryReceipts;
        rec.client_cat = luv::ClientCategory::Professional;
        std::strncpy(rec.venue_mic, "EDGA", 4);
        std::strncpy(rec.venue_lei, "549300EEEEEEEEEEEEEE", 20);
        rec.gross_notional = 2000.0;
        assert(reporter.record_execution(rec));
    }

    assert(reporter.total_records_processed() == 1275);

    auto report = reporter.generate_report(luv::InstrumentClass::Equities_SharesAndDepositaryReceipts,
                                           luv::ClientCategory::Professional);

    std::cout << "  Class Total Notional: $" << report.total_class_notional << std::endl;
    std::cout << "  Class Total Orders: " << report.total_class_orders << std::endl;
    std::cout << "  Top Venues Extracted: " << report.venue_count << std::endl;

    assert(report.venue_count == 5);
    assert(report.total_class_orders == 1275);
    assert(std::abs(report.total_class_notional - 2550000.0) < 1.0);

    // Verify Rank 1: XNAS (1,000,000 / 2,550,000 = ~39.215%)
    assert(std::strncmp(report.top_venues[0].venue_mic, "XNAS", 4) == 0);
    assert(std::abs(report.top_venues[0].volume_percentage - (1000000.0 / 2550000.0 * 100.0)) < 0.01);
    assert(std::abs(report.top_venues[0].passive_percentage - 60.0) < 0.01); // 300/500 = 60%
    assert(std::abs(report.top_venues[0].aggressive_percentage - 40.0) < 0.01); // 200/500 = 40%
    assert(std::abs(report.top_venues[0].directed_percentage - 10.0) < 0.01); // 50/500 = 10%

    // Verify Rank 2: XNYS (800,000 / 2,550,000)
    assert(std::strncmp(report.top_venues[1].venue_mic, "XNYS", 4) == 0);
    assert(std::abs(report.top_venues[1].passive_percentage - 50.0) < 0.01);

    // Verify Rank 3: BATS
    assert(std::strncmp(report.top_venues[2].venue_mic, "BATS", 4) == 0);

    // Verify Rank 4: XCBO
    assert(std::strncmp(report.top_venues[3].venue_mic, "XCBO", 4) == 0);

    // Verify Rank 5: IEXG
    assert(std::strncmp(report.top_venues[4].venue_mic, "IEXG", 4) == 0);

    std::cout << "[PASS] MiFID II RTS 28 Venue Execution Reporter Tests Passed!" << std::endl;
    return 0;
}
