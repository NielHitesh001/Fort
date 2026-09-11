#pragma once

#include <atomic>
#include <cstdint>

namespace luv {

enum class LogLevel : uint8_t { kDebug, kInfo, kWarn, kError, kFatal };

struct LogEvent {
    LogLevel level = LogLevel::kInfo;
    const char* module = "";
    const char* event = "";
    int64_t value1 = 0;
    int64_t value2 = 0;
    const char* details = "";
};

using LogSink = void (*)(const LogEvent&) noexcept;

// A process-wide, allocation-free hook for error paths. The default is silent
// so simulations retain their existing output behavior; an application can
// install a sink that forwards events to its telemetry or logging system.
class StructuredLogger {
public:
    static void set_sink(LogSink sink) noexcept {
        _sink.store(sink, std::memory_order_release);
    }

    [[nodiscard]] static LogSink sink() noexcept {
        return _sink.load(std::memory_order_acquire);
    }

    static void log(LogLevel level, const char* module, const char* event,
                    int64_t value1 = 0, int64_t value2 = 0,
                    const char* details = "") noexcept {
        LogSink target = _sink.load(std::memory_order_acquire);
        if (!target) return;
        target(LogEvent{level, module ? module : "", event ? event : "",
                        value1, value2, details ? details : ""});
    }

private:
    inline static std::atomic<LogSink> _sink{nullptr};
};

}  // namespace luv
