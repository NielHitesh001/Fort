#include <iostream>
#include <cassert>
#include <vector>
#include "luv_nanosecond_replay.hpp"

int main() {
    std::cout << "[TEST] Running Nanosecond Market Replay & Event Scheduler Test...\n";

    luv::NanosecondReplayScheduler scheduler;

    // Schedule events out of chronological order to verify automatic chronological sorting
    luv::ReplayEvent e3{3000, 3, 103, 1, 1'003'000, 300, true, luv::ReplayEventType::AddOrder};
    luv::ReplayEvent e1{1000, 1, 101, 1, 1'001'000, 100, true, luv::ReplayEventType::AddOrder};
    luv::ReplayEvent e2{2000, 2, 102, 1, 1'002'000, 200, false, luv::ReplayEventType::AddOrder};
    luv::ReplayEvent e4{4000, 4, 101, 1, 1'001'000, 100, true, luv::ReplayEventType::CancelOrder};

    assert(scheduler.schedule_event(e3));
    assert(scheduler.schedule_event(e1));
    assert(scheduler.schedule_event(e2));
    assert(scheduler.schedule_event(e4));

    assert(scheduler.get_total_events() == 4);
    assert(scheduler.get_processed_count() == 0);

    // Step 1: Advance simulation to t = 2500 ns -> Should process events at 1000 and 2000
    std::vector<uint64_t> processed_seqs;
    size_t count1 = scheduler.step_until(2500, [&](const luv::ReplayEvent& evt) {
        processed_seqs.push_back(evt.sequence_no);
    });

    assert(count1 == 2);
    assert(processed_seqs.size() == 2);
    assert(processed_seqs[0] == 1); // e1 at 1000ns
    assert(processed_seqs[1] == 2); // e2 at 2000ns
    assert(scheduler.get_sim_time() == 2500);

    // Step 2: Advance simulation to t = 5000 ns -> Should process events at 3000 and 4000
    size_t count2 = scheduler.step_until(5000, [&](const luv::ReplayEvent& evt) {
        processed_seqs.push_back(evt.sequence_no);
    });

    assert(count2 == 2);
    assert(processed_seqs.size() == 4);
    assert(processed_seqs[2] == 3); // e3 at 3000ns
    assert(processed_seqs[3] == 4); // e4 at 4000ns
    assert(scheduler.is_finished());

    std::cout << "[TEST] Replayed " << processed_seqs.size() << " events in perfect nanosecond order.\n";
    std::cout << "[TEST] Nanosecond Market Replay & Event Scheduler Test Passed!\n";
    return 0;
}
