#pragma once

#include <cstdint>
#include <array>
#include <cstring>
#include "luv_fix.hpp"

namespace luv {
namespace dropcopy {

struct DropCopyMessage {
    uint64_t seq_num = 0;
    uint32_t firm_id = 0;
    uint16_t symbol_idx = 0;
    char msg_type = '8'; // ExecutionReport
    char exec_type = '0';
    int64_t price = 0;
    int64_t qty = 0;
    uint64_t order_id = 0;
    uint64_t exec_id = 0;
    uint64_t ts_ns = 0;
};

class DropCopySession {
public:
    static constexpr size_t kReplayBufferSize = 1024;

    explicit DropCopySession(std::string_view subscriber_id, std::string_view target_id) noexcept
        : subscriber_id_(subscriber_id), target_id_(target_id), seq_num_(1), count_(0) {}

    // Ingests matching engine event and generates drop copy record
    bool publish_execution(
        uint32_t firm_id,
        uint16_t symbol_idx,
        char exec_type,
        int64_t price,
        int64_t qty,
        uint64_t order_id,
        uint64_t exec_id,
        uint64_t ts_ns,
        DropCopyMessage& out_msg) noexcept
    {
        const uint64_t cur_seq = seq_num_++;
        out_msg = DropCopyMessage{
            .seq_num = cur_seq,
            .firm_id = firm_id,
            .symbol_idx = symbol_idx,
            .msg_type = '8',
            .exec_type = exec_type,
            .price = price,
            .qty = qty,
            .order_id = order_id,
            .exec_id = exec_id,
            .ts_ns = ts_ns
        };

        buffer_[count_ & (kReplayBufferSize - 1)] = out_msg;
        count_++;
        return true;
    }

    // Handles ResendRequest (35=2) for historical replay
    size_t replay_range(
        uint64_t begin_seq,
        uint64_t end_seq,
        DropCopyMessage* out_replay_buf,
        size_t max_replay_count) const noexcept
    {
        if (!out_replay_buf || max_replay_count == 0 || begin_seq > end_seq) return 0;

        size_t written = 0;
        const size_t total_avail = std::min<size_t>(count_, kReplayBufferSize);

        for (size_t i = 0; i < total_avail && written < max_replay_count; ++i) {
            size_t idx = (count_ - total_avail + i) & (kReplayBufferSize - 1);
            if (buffer_[idx].seq_num >= begin_seq && buffer_[idx].seq_num <= end_seq) {
                out_replay_buf[written++] = buffer_[idx];
            }
        }

        return written;
    }

    uint64_t next_seq_num() const noexcept { return seq_num_; }

private:
    std::string_view subscriber_id_;
    std::string_view target_id_;
    uint64_t seq_num_{1};
    size_t count_{0};
    std::array<DropCopyMessage, kReplayBufferSize> buffer_{};
};

} // namespace dropcopy
} // namespace luv
