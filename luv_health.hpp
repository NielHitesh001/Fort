#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "luv_arena.hpp"
#include "luv_safety.hpp"

namespace luv {

struct TraceContext {
    uint64_t trace_id = 0;
    uint64_t span_id = 0;
    uint64_t parent_span_id = 0;
    uint64_t timestamp_ns = 0;
};

enum class HealthProbeStatus : uint8_t {
    kHealthy = 0,
    kDegraded = 1,
    kUnhealthy = 2,
};

class SystemHealthMonitor {
public:
    static HealthProbeStatus evaluate(
        const Arena& arena,
        const CircuitBreaker& cb,
        uint64_t max_acceptable_drop_rate = 100) noexcept
    {
        if (cb.is_open()) {
            return HealthProbeStatus::kUnhealthy;
        }

        const auto memory_tier = arena.tick_ring.pressure_tier();
        if (memory_tier == SPSCRing<TickMsg, (1u << 22)>::MemoryPressureTier::kEmergencyHalt) {
            return HealthProbeStatus::kUnhealthy;
        }

        if (cb.is_half_open() ||
            memory_tier == SPSCRing<TickMsg, (1u << 22)>::MemoryPressureTier::kShedLoad ||
            memory_tier == SPSCRing<TickMsg, (1u << 22)>::MemoryPressureTier::kWarning ||
            arena.telemetry_drops.load(std::memory_order_relaxed) > max_acceptable_drop_rate)
        {
            return HealthProbeStatus::kDegraded;
        }

        return HealthProbeStatus::kHealthy;
    }

    static bool format_healthz_json(
        HealthProbeStatus status,
        const Arena& arena,
        const CircuitBreaker& cb,
        char* buffer,
        size_t buffer_size) noexcept
    {
        if (!buffer || buffer_size == 0) return false;

        const char* status_str = "HEALTHY";
        switch (status) {
            case HealthProbeStatus::kHealthy: status_str = "HEALTHY"; break;
            case HealthProbeStatus::kDegraded: status_str = "DEGRADED"; break;
            case HealthProbeStatus::kUnhealthy: status_str = "UNHEALTHY"; break;
        }

        const char* cb_str = cb.is_closed() ? "CLOSED" : (cb.is_open() ? "OPEN" : "HALF_OPEN");

        const int written = std::snprintf(
            buffer, buffer_size,
            "{\n"
            "  \"status\": \"%s\",\n"
            "  \"circuit_breaker\": \"%s\",\n"
            "  \"ring_occupancy_pct\": %.2f,\n"
            "  \"telemetry_drops\": %llu\n"
            "}\n",
            status_str,
            cb_str,
            arena.tick_ring.occupancy_ratio() * 100.0,
            static_cast<unsigned long long>(arena.telemetry_drops.load(std::memory_order_relaxed)));

        return written > 0 && static_cast<size_t>(written) < buffer_size;
    }
};

} // namespace luv
