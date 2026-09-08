#pragma once

#include <cstdint>
#include <vector>
#include <queue>
#include <random>
#include <cstring>
#include <span>

namespace luv {
namespace chaos {

struct ChaosConfig {
    double packet_drop_rate = 0.0;       // e.g. 0.05 for 5% loss
    double packet_reorder_rate = 0.0;    // e.g. 0.10 for 10% reordering
    double packet_duplicate_rate = 0.0;  // e.g. 0.02 for 2% duplicate
    double payload_corruption_rate = 0.0;// e.g. 0.01 for 1% corrupted packets
    uint32_t reorder_depth = 4;          // Max reorder window depth
    uint64_t seed = 42;
};

struct PacketPayload {
    std::vector<uint8_t> data;
    uint64_t arrival_ts_ns = 0;
};

class NetworkChaosInjector {
public:
    explicit NetworkChaosInjector(const ChaosConfig& config) noexcept
        : config_(config), rng_(config.seed), dist_(0.0, 1.0) {}

    // Ingests a packet and processes it through the chaos model.
    // Returns output packets ready for feed handler ingestion.
    template <typename Callback>
    void inject_and_process(const uint8_t* data, size_t len, uint64_t ts_ns, Callback&& emit_cb) {
        if (!data || len == 0) return;

        // 1. Packet Loss Check
        if (config_.packet_drop_rate > 0.0 && dist_(rng_) < config_.packet_drop_rate) {
            dropped_count_++;
            return; // Dropped
        }

        // 2. Packet Corruption Check (bit flipping)
        std::vector<uint8_t> packet_data(data, data + len);
        if (config_.payload_corruption_rate > 0.0 && dist_(rng_) < config_.payload_corruption_rate) {
            if (!packet_data.empty()) {
                packet_data[packet_data.size() / 2] ^= 0xFF; // Corrupt payload byte
                corrupted_count_++;
            }
        }

        // 3. Packet Duplication Check
        bool duplicate = (config_.packet_duplicate_rate > 0.0 && dist_(rng_) < config_.packet_duplicate_rate);

        // 4. Packet Reordering Check
        if (config_.packet_reorder_rate > 0.0 && dist_(rng_) < config_.packet_reorder_rate) {
            reordered_queue_.push_back(PacketPayload{packet_data, ts_ns});
            reordered_count_++;

            if (reordered_queue_.size() >= config_.reorder_depth) {
                // Flush oldest buffered packet
                emit_cb(reordered_queue_.front().data.data(), reordered_queue_.front().data.size(), reordered_queue_.front().arrival_ts_ns);
                reordered_queue_.erase(reordered_queue_.begin());
            }
            return;
        }

        // Normal Delivery
        emit_cb(packet_data.data(), packet_data.size(), ts_ns);
        delivered_count_++;

        if (duplicate) {
            duplicated_count_++;
            emit_cb(packet_data.data(), packet_data.size(), ts_ns + 10);
        }

        // Periodically drain any reordered packets
        if (!reordered_queue_.empty() && dist_(rng_) < 0.5) {
            emit_cb(reordered_queue_.back().data.data(), reordered_queue_.back().data.size(), reordered_queue_.back().arrival_ts_ns);
            reordered_queue_.pop_back();
        }
    }

    // Flush any remaining buffered packets
    template <typename Callback>
    void flush(Callback&& emit_cb) {
        while (!reordered_queue_.empty()) {
            emit_cb(reordered_queue_.front().data.data(), reordered_queue_.front().data.size(), reordered_queue_.front().arrival_ts_ns);
            reordered_queue_.erase(reordered_queue_.begin());
        }
    }

    uint64_t dropped_count() const noexcept { return dropped_count_; }
    uint64_t reordered_count() const noexcept { return reordered_count_; }
    uint64_t duplicated_count() const noexcept { return duplicated_count_; }
    uint64_t corrupted_count() const noexcept { return corrupted_count_; }
    uint64_t delivered_count() const noexcept { return delivered_count_; }

private:
    ChaosConfig config_;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> dist_;
    std::vector<PacketPayload> reordered_queue_;

    uint64_t dropped_count_{0};
    uint64_t reordered_count_{0};
    uint64_t duplicated_count_{0};
    uint64_t corrupted_count_{0};
    uint64_t delivered_count_{0};
};

} // namespace chaos
} // namespace luv
