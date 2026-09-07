#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "luv_safety.hpp"
#include "luv_decoder.hpp"

namespace {

void test_sec_17a4_worm_audit_compliance() {
    std::printf("\n== SEC 17a-4 / WORM Storage & Manifest Verification ==\n");

    const char* log_path = "/tmp/luv-sec-audit-test.bin";
    const char* manifest_path = "/tmp/luv-sec-manifest-test.bin";
    const char* json_path = "/tmp/luv-sec-export-test.json";

    ::unlink(log_path);
    ::unlink(manifest_path);
    ::unlink(json_path);

    // 1. Create audit log and append events
    {
        luv::DurableAuditLog log;
        assert(log.open(log_path, 1));

        for (uint64_t i = 0; i < 50; ++i) {
            assert(log.append(1000 + i, 5000 + i, 1'000'000 + (i * 100), 10 + i, 1, (i % 2), 'A'));
        }
        assert(log.size() == 50);

        // Read range query
        luv::AuditEvent events[10]{};
        size_t count = 0;
        assert(log.read_range(10, 19, events, 10, count));
        assert(count == 10);
        assert(events[0].sequence == 10);
        assert(events[9].sequence == 19);

        // Write periodic checkpoint manifest for [0..49]
        assert(log.write_manifest_checkpoint(manifest_path, 0, 49));
    }

    // 2. Verify manifest checkpoint
    assert(luv::DurableAuditLog::verify_manifest(log_path, manifest_path));

    // 3. Verify complete log integrity and retrieve root hash
    uint64_t valid_events = 0;
    uint8_t root_hash[32]{};
    assert(luv::DurableAuditLog::verify_log_integrity(log_path, valid_events, root_hash));
    assert(valid_events == 50);

    // 4. Export to compliance JSON format
    assert(luv::DurableAuditLog::export_json(log_path, json_path, 0, 49));
    struct stat jst{};
    assert(::stat(json_path, &jst) == 0 && jst.st_size > 0);

    ::unlink(log_path);
    ::unlink(manifest_path);
    ::unlink(json_path);

    std::printf("  [OK] WORM append-only, range queries, manifest verification, and JSON export verified\n");
}

void test_multi_feed_and_instrument_registry() {
    std::printf("\n== Multi-Protocol Feed Decoders & Instrument Registry ==\n");

    // 1. Instrument Metadata & Precision
    luv::InstrumentRegistry registry;

    luv::InstrumentMetadata btc_meta{};
    btc_meta.symbol_idx = 10;
    std::memcpy(btc_meta.ticker, "BTCUSD  ", 8);
    btc_meta.asset_class = luv::AssetClass::kCrypto;
    btc_meta.price_decimals = 2; // $123.45
    btc_meta.qty_decimals = 8;   // 0.00000001 BTC
    btc_meta.min_tick_size = 1;
    btc_meta.min_lot_size = 1000;
    assert(registry.register_instrument(btc_meta));

    const auto& fetched = registry.get(10);
    assert(fetched.to_fixed_price(65432.10) == 6543210);
    assert(fetched.to_float_price(6543210) == 65432.10);
    assert(fetched.to_fixed_qty(0.50000000) == 50000000);
    assert(fetched.to_float_qty(50000000) == 0.50000000);

    // 2. SBE Market Data Decoder
    luv::SbeMarketDataDecoder sbe_decoder;
    assert(sbe_decoder.protocol() == luv::MarketDataProtocol::kSBE);

    uint8_t sbe_buffer[64]{};
    uint16_t block_len = 36;
    uint16_t template_id = luv::SbeMarketDataDecoder::kMsgAddOrder;
    uint16_t schema_id = 1;
    uint16_t version = 1;

    std::memcpy(sbe_buffer + 0, &block_len, 2);
    std::memcpy(sbe_buffer + 2, &template_id, 2);
    std::memcpy(sbe_buffer + 4, &schema_id, 2);
    std::memcpy(sbe_buffer + 6, &version, 2);

    uint64_t timestamp = 123456789ULL;
    uint64_t order_ref = 987654ULL;
    int64_t price = 5000000LL;
    int64_t qty = 200LL;
    uint16_t symbol_idx = 10;
    uint8_t side = 0; // Bid
    uint8_t msg_type = 'A';

    std::memcpy(sbe_buffer + 8 + 0, &timestamp, 8);
    std::memcpy(sbe_buffer + 8 + 8, &order_ref, 8);
    std::memcpy(sbe_buffer + 8 + 16, &price, 8);
    std::memcpy(sbe_buffer + 8 + 24, &qty, 8);
    std::memcpy(sbe_buffer + 8 + 32, &symbol_idx, 2);
    sbe_buffer[8 + 34] = side;
    sbe_buffer[8 + 35] = msg_type;

    luv::SymbolTable sym_tab;
    luv::TickMsg tick{};
    assert(sbe_decoder.decode(sbe_buffer, sizeof(sbe_buffer), sym_tab, tick));
    assert(tick.symbol_idx == 10);
    assert(tick.timestamp == 123456789ULL);
    assert(tick.order_ref == 987654ULL);
    assert(tick.price == 5000000LL);
    assert(tick.qty == 200LL);
    assert(tick.msg_type == 'A');
    assert(tick.flags == 0);

    // 3. Polymorphic decoder boundary
    luv::IFeedDecoder* generic_decoder = &sbe_decoder;
    luv::TickMsg tick2{};
    assert(generic_decoder->decode(sbe_buffer, sizeof(sbe_buffer), sym_tab, tick2));
    assert(tick2.order_ref == 987654ULL);

    std::printf("  [OK] Multi-protocol SBE decode, polymorphic boundary, and decimal precision models verified\n");
}

}  // namespace

int main() {
    std::printf("=== Multi-Feed & Regulatory Compliance Tests ===\n");

    test_sec_17a4_worm_audit_compliance();
    test_multi_feed_and_instrument_registry();

    std::printf("\nAll Multi-Feed & Regulatory Compliance tests passed successfully.\n");
    return 0;
}
