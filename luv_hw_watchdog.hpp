#pragma once

#include <cstdint>
#include <array>
#include <algorithm>

namespace luv {
namespace hw {

struct LatencyJitterSample {
    uint64_t timestamp_ns = 0;
    uint64_t measured_latency_ns = 0;
    uint32_t cpu_core_id = 0;
    bool is_jitter_anomaly = false;
};

struct HardwareAnomalyReport {
    uint64_t total_samples = 0;
    uint64_t anomaly_count = 0;
    uint64_t p50_latency_ns = 0;
    uint64_t p99_latency_ns = 0;
    uint64_t max_latency_ns = 0;
    bool core_isolation_violated = false;
};

class HardwareLatencyWatchdog {
public:
    static constexpr size_t kMaxSamples = 1024;
    static constexpr uint64_t kJitterThresholdNs = 2000; // 2.0 microseconds jitter threshold

    explicit HardwareLatencyWatchdog(uint64_t jitter_threshold_ns = kJitterThresholdNs) noexcept
        : threshold_ns_(jitter_threshold_ns), num_samples_(0), anomaly_count_(0) {}

    void record_sample(uint64_t timestamp_ns, uint64_t latency_ns, uint32_t cpu_core_id) noexcept {
        if (num_samples_ >= kMaxSamples) return;

        bool anomaly = (latency_ns > threshold_ns_);
        if (anomaly) {
            anomaly_count_++;
        }

        samples_[num_samples_++] = LatencyJitterSample{
            .timestamp_ns = timestamp_ns,
            .measured_latency_ns = latency_ns,
            .cpu_core_id = cpu_core_id,
            .is_jitter_anomaly = anomaly
        };
    }

    HardwareAnomalyReport generate_report() const noexcept {
        HardwareAnomalyReport report{};
        report.total_samples = num_samples_;
        report.anomaly_count = anomaly_count_;

        if (num_samples_ == 0) return report;

        std::array<uint64_t, kMaxSamples> sorted_latencies{};
        for (size_t i = 0; i < num_samples_; ++i) {
            sorted_latencies[i] = samples_[i].measured_latency_ns;
        }

        std::sort(sorted_latencies.begin(), sorted_latencies.begin() + num_samples_);

        report.p50_latency_ns = sorted_latencies[num_samples_ / 2];
        size_t p99_idx = (num_samples_ * 99) / 100;
        if (p99_idx >= num_samples_) p99_idx = num_samples_ - 1;
        report.p99_latency_ns = sorted_latencies[p99_idx];
        report.max_latency_ns = sorted_latencies[num_samples_ - 1];
        report.core_isolation_violated = (anomaly_count_ > 0);

        return report;
    }

    void reset() noexcept {
        num_samples_ = 0;
        anomaly_count_ = 0;
    }

private:
    uint64_t threshold_ns_{kJitterThresholdNs};
    std::array<LatencyJitterSample, kMaxSamples> samples_{};
    size_t num_samples_{0};
    uint64_t anomaly_count_{0};
};

} // namespace hw
} // namespace luv
