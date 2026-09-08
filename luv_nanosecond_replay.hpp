#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

enum class ReplayEventType : uint8_t {
    AddOrder = 0,
    ModifyOrder = 1,
    CancelOrder = 2,
    TradeExecution = 3,
    Snapshot = 4
};

struct ReplayEvent {
    uint64_t timestamp_ns{0};
    uint64_t sequence_no{0};
    uint64_t order_id{0};
    uint64_t symbol_id{0};
    uint64_t price{0};
    uint64_t quantity{0};
    bool is_buy{true};
    ReplayEventType type{ReplayEventType::AddOrder};
};

class NanosecondReplayScheduler {
public:
    static constexpr size_t kMaxEvents = 512;

    NanosecondReplayScheduler() noexcept : event_count_(0), processed_count_(0), current_sim_time_ns_(0) {}

    bool schedule_event(const ReplayEvent& evt) noexcept {
        if (event_count_ >= kMaxEvents) return false;

        // Insertion sort into chronological order
        size_t idx = event_count_;
        events_[event_count_++] = evt;

        while (idx > 0 && events_[idx].timestamp_ns < events_[idx - 1].timestamp_ns) {
            std::swap(events_[idx], events_[idx - 1]);
            --idx;
        }

        return true;
    }

    // Step simulation up to max_ns
    template <typename HandlerFunc>
    size_t step_until(uint64_t target_time_ns, HandlerFunc&& handler) noexcept {
        size_t events_dispatched = 0;

        while (processed_count_ < event_count_) {
            const auto& evt = events_[processed_count_];
            if (evt.timestamp_ns > target_time_ns) {
                break;
            }

            current_sim_time_ns_ = evt.timestamp_ns;
            handler(evt);
            ++processed_count_;
            ++events_dispatched;
        }

        if (current_sim_time_ns_ < target_time_ns) {
            current_sim_time_ns_ = target_time_ns;
        }

        return events_dispatched;
    }

    uint64_t get_sim_time() const noexcept { return current_sim_time_ns_; }
    size_t get_processed_count() const noexcept { return processed_count_; }
    size_t get_total_events() const noexcept { return event_count_; }
    bool is_finished() const noexcept { return processed_count_ == event_count_; }

    void reset() noexcept {
        processed_count_ = 0;
        current_sim_time_ns_ = 0;
    }

private:
    std::array<ReplayEvent, kMaxEvents> events_{};
    size_t event_count_{0};
    size_t processed_count_{0};
    uint64_t current_sim_time_ns_{0};
};

} // namespace luv
