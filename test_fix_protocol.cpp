#include "luv_fix.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

void test_fix_parser_new_order_single() {
    // Standard FIX 4.4 NewOrderSingle with pipe delimiters for easy readability
    const std::string raw = "8=FIX.4.4|9=75|35=D|49=CLIENT|56=LUV_EXCH|34=1|52=20260908-00:00:00|11=1001|55=AAPL|54=1|38=500|44=15000|10=000|";
    luv::fix::FixMessageView view;
    bool ok = view.parse(raw.data(), raw.size());
    assert(ok);

    assert(view.msg_type() == "D");
    assert(view.seq_num() == 1);
    assert(view.get(luv::fix::Tag::SenderCompID) == "CLIENT");
    assert(view.get(luv::fix::Tag::TargetCompID) == "LUV_EXCH");
    assert(view.get(luv::fix::Tag::ClOrdID) == "1001");
    assert(view.get(luv::fix::Tag::Symbol) == "AAPL");
    assert(view.get_char(luv::fix::Tag::Side) == '1');
    assert(view.get_int(luv::fix::Tag::OrderQty) == 500);
    assert(view.get_int(luv::fix::Tag::Price) == 15000);

    std::printf("[PASS] test_fix_parser_new_order_single\n");
}

void test_fix_session_and_execution_report() {
    luv::fix::FixSession session("LUV_EXCH", "CLIENT");
    assert(session.state() == luv::fix::SessionState::kDisconnected);

    session.on_logon();
    assert(session.state() == luv::fix::SessionState::kActive);

    // Test sequence gap detection
    bool gap = false;
    uint64_t expected = 0;
    bool valid = session.handle_inbound_seq(5, gap, expected);
    assert(!valid);
    assert(gap);
    assert(expected == 1);

    // Ingest sequential
    valid = session.handle_inbound_seq(1, gap, expected);
    assert(valid);
    assert(!gap);
    assert(session.next_in_seq() == 2);

    // Format ExecutionReport
    char buf[512];
    size_t len = session.format_execution_report(
        buf, sizeof(buf),
        1001, 50001, 90001,
        '2', // Fill
        '2', // Filled
        "AAPL",
        '1', // Buy
        500,
        15000,
        500,
        0
    );
    assert(len > 0);

    // Verify CheckSum on generated message
    bool ck_valid = luv::fix::FixMessageView::verify_checksum(buf, len);
    assert(ck_valid);

    // Parse back generated message
    luv::fix::FixMessageView exec_view;
    bool parse_ok = exec_view.parse(buf, len);
    assert(parse_ok);
    assert(exec_view.msg_type() == "8");
    assert(exec_view.get_char(luv::fix::Tag::ExecType) == '2');
    assert(exec_view.get_int(luv::fix::Tag::CumQty) == 500);
    assert(exec_view.get_int(luv::fix::Tag::LeavesQty) == 0);

    std::printf("[PASS] test_fix_session_and_execution_report\n");
}

void test_fix_heartbeat_and_logon() {
    luv::fix::FixSession session("LUV_EXCH", "CLIENT");
    char buf[512];

    size_t logon_len = session.format_logon(buf, sizeof(buf), 30);
    assert(logon_len > 0);
    assert(luv::fix::FixMessageView::verify_checksum(buf, logon_len));

    size_t hb_len = session.format_heartbeat(buf, sizeof(buf), "TEST-123");
    assert(hb_len > 0);
    assert(luv::fix::FixMessageView::verify_checksum(buf, hb_len));

    luv::fix::FixMessageView hb_view;
    assert(hb_view.parse(buf, hb_len));
    assert(hb_view.msg_type() == "0");
    assert(hb_view.get(luv::fix::Tag::TestReqID) == "TEST-123");

    std::printf("[PASS] test_fix_heartbeat_and_logon\n");
}

int main() {
    test_fix_parser_new_order_single();
    test_fix_session_and_execution_report();
    test_fix_heartbeat_and_logon();
    std::printf("All FIX protocol tests passed successfully.\n");
    return 0;
}
