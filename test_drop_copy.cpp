#include "luv_drop_copy.hpp"
#include <cassert>
#include <cstdio>

void test_drop_copy_feed_and_replay() {
    luv::dropcopy::DropCopySession session("PRIME_BROKER_1", "LUV_EXCHANGE");

    // Publish 5 execution events
    for (uint64_t i = 1; i <= 5; ++i) {
        luv::dropcopy::DropCopyMessage msg;
        assert(session.publish_execution(
            100, 1, '2', 15000 + static_cast<int64_t>(i), 100 * static_cast<int64_t>(i), 1000 + i, 5000 + i, i * 1000, msg));
        assert(msg.seq_num == i);
        assert(msg.price == 15000 + static_cast<int64_t>(i));
    }
    assert(session.next_seq_num() == 6);

    // Replay sequence range 2 -> 4 (3 messages expected)
    luv::dropcopy::DropCopyMessage replay_buf[10];
    size_t replayed = session.replay_range(2, 4, replay_buf, 10);
    assert(replayed == 3);
    assert(replay_buf[0].seq_num == 2);
    assert(replay_buf[1].seq_num == 3);
    assert(replay_buf[2].seq_num == 4);

    std::printf("[PASS] test_drop_copy_feed_and_replay\n");
}

int main() {
    test_drop_copy_feed_and_replay();
    std::printf("All FIX Drop Copy session tests passed successfully.\n");
    return 0;
}
