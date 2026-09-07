#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "luv_arena.hpp"
#include "luv_ai.hpp"
#include "luv_consumer.hpp"
#include "luv_feed_sim.hpp"
#include "luv_telemetry.hpp"

namespace {

constexpr const char* kModelPath = "/tmp/luv_linear_model.bin";

void write_linear_model() {
    int fd = ::open(kModelPath, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    assert(fd >= 0);

    luv::ai::ModelHeader header {};
    header.model_id = 7;
    header.input_floats = luv::FeatureRow::kFloats;
    header.payload_offset = sizeof(luv::ai::ModelHeader);
    header.payload_bytes = luv::FeatureRow::kFloats * sizeof(float);
    header.bias = -0.25f;

    float weights[luv::FeatureRow::kFloats] = {};
    weights[0] = 0.5f;
    weights[15 * luv::Config::kLookbackSteps] = 0.25f;

    assert(::write(fd, &header, sizeof(header)) == sizeof(header));
    assert(::write(fd, weights, sizeof(weights)) == sizeof(weights));
    ::close(fd);
}

uint16_t bind_udp_receiver(int& fd) {
    fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);

    timeval tv {};
    tv.tv_sec = 1;
    assert(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0);

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    assert(::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    socklen_t len = sizeof(addr);
    assert(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    return ntohs(addr.sin_port);
}

void test_ai_engine() {
    std::printf("\n== AI engine ==\n");

    write_linear_model();

    luv::Arena arena;
    assert(arena.init());

    luv::FeatureRow& row = arena.feature_rows[0];
    row.data[0] = 2.0f;
    row.data[15 * luv::Config::kLookbackSteps] = 4.0f;

    luv::AIEngine ai;
    assert(ai.init(arena));
    assert(ai.load_model_file(kModelPath));
    assert(ai.model_base() == arena.ai_region);
    assert(ai.model_size() >= sizeof(luv::ai::ModelHeader));

    assert(ai.infer_symbol(0));

    const luv::SignalOutput& sig = arena.signal_slots[0];
    assert(sig.direction == 1);
    assert(sig.model_id == 7);
    assert(sig.expected_move > 1.74f && sig.expected_move < 1.76f);
    assert(sig.confidence > 0.5f);
    assert(ai.inference_count() == 1);

    std::printf("  [OK] mapped model -> features -> signal\n");
}

void test_telemetry_bridge() {
    std::printf("\n== Telemetry bridge ==\n");

    luv::Arena arena;
    assert(arena.init());

    arena.exec_states[0].risk.daily_pnl = 12345;
    arena.exec_states[0].risk.gross_exposure = 98765;
    arena.exec_states[0].risk.order_count = 3;
    arena.exec_states[0].orders[0].filled_qty = 10;

    assert(luv::TelemetryPublisher::publish_heartbeat(
        arena, 42'000, 3.5f, 99.0f));

    luv::TelemSnapshot* peeked = arena.telem_ring.try_peek();
    assert(peeked != nullptr);
    assert(peeked->session_pnl == 12345);
    assert(peeked->gross_exposure == 98765);
    assert(peeked->active_orders == 3);
    assert(peeked->fill_count == 1);

    int rx_fd = -1;
    const uint16_t port = bind_udp_receiver(rx_fd);

    luv::TelemetryBridgeConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = port;
    cfg.format = luv::TelemetryWireFormat::kJson;
    cfg.max_batch = 8;

    luv::TelemetryBridge bridge;
    assert(bridge.init(arena, cfg));
    assert(bridge.pump_once() == 1);
    assert(bridge.snapshots_sent() == 1);

    char buf[1024] = {};
    const ssize_t n = ::recvfrom(rx_fd, buf, sizeof(buf) - 1, 0, nullptr, nullptr);
    assert(n > 0);
    buf[n] = '\0';
    assert(std::strstr(buf, "\"type\":\"luv_heartbeat\"") != nullptr);
    assert(std::strstr(buf, "\"session_pnl\":12345") != nullptr);
    assert(std::strstr(buf, "\"active_orders\":3") != nullptr);
    ::close(rx_fd);

    std::printf("  [OK] heartbeat ring -> UDP dashboard packet\n");
}

int fault_predict(const float*, uint32_t, float* score) {
    *score = 1.0f;
    return 0;
}

void test_fault_injected_ai_feed() {
    std::printf("\n== Fault-injected AI feed ==\n");

    luv::Arena arena;
    assert(arena.init());
    luv::AIEngine ai;
    assert(ai.init(arena));
    ai.bind_predict_fn(&fault_predict, 9);

    luv::SimConfig cfg{};
    cfg.synthetic_symbols = 16;
    cfg.target_rate_hz = 0;
    cfg.prebuf_count = 128;
    cfg.seed = 0xA5A5;
    cfg.drop_rate = 0.4;
    cfg.reorder_window = 32;
    cfg.burst_size = 24;
    cfg.burst_interval = 6;

    luv::SimFeedSource feed(cfg);
    assert(feed.init(arena));
    luv::Consumer consumer;
    assert(consumer.init(arena));
    consumer.set_ai_engine(&ai);

    constexpr uint32_t attempts = 2'000;
    for (uint32_t i = 0; i < attempts; ++i) {
        (void)feed.poll();
        while (consumer.process_one()) {}
    }
    while (consumer.process_one()) {}

    assert(feed.total_dropped() + feed.total_messages() == attempts);
    assert(ai.inference_count() == consumer.ticks_processed());
    assert(consumer.lob().active_order_count() <
           luv::LOBEngine::order_ref_map_capacity());
    std::printf("  [OK] attempts=%u dropped=%llu decoded=%llu inferred=%llu\n",
                attempts,
                static_cast<unsigned long long>(feed.total_dropped()),
                static_cast<unsigned long long>(feed.total_messages()),
                static_cast<unsigned long long>(ai.inference_count()));
}

}  // namespace

int main() {
    std::printf("AI + Telemetry Integration Test\n");

    test_ai_engine();
    test_telemetry_bridge();
    test_fault_injected_ai_feed();

    std::printf("\nAll AI/telemetry tests passed.\n");
    return 0;
}
