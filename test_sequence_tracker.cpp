#include <cassert>
#include <cstdio>

#include "luv_safety.hpp"

int main() {
    luv::SequenceTracker tracker;

    assert(tracker.observe(100) == luv::SequenceResult::kFirst);
    assert(tracker.initialized());
    assert(tracker.next() == 101);

    assert(tracker.observe(101) == luv::SequenceResult::kNext);
    assert(tracker.next() == 102);

    assert(tracker.observe(104) == luv::SequenceResult::kGap);
    assert(tracker.gaps() == 2);
    assert(tracker.next() == 105);

    assert(tracker.observe(104) == luv::SequenceResult::kDuplicate);
    assert(tracker.gaps() == 2);

    assert(tracker.observe(102) == luv::SequenceResult::kOutOfOrder);
    assert(tracker.next() == 105);

    tracker.reset();
    assert(!tracker.initialized());
    assert(tracker.gaps() == 0);
    assert(tracker.observe(7) == luv::SequenceResult::kFirst);

    tracker.reset();
    assert(tracker.observe(1000) == luv::SequenceResult::kFirst);
    assert(tracker.observe(1011) == luv::SequenceResult::kGap);
    assert(tracker.gap_class() == luv::SequenceGapClass::kRecoverable);
    assert(tracker.gap_pending());
    assert(tracker.last_gap_size() == 10);

    tracker.reset();
    assert(tracker.observe(2000) == luv::SequenceResult::kFirst);
    assert(tracker.observe(2501) == luv::SequenceResult::kGap);
    assert(tracker.gap_class() == luv::SequenceGapClass::kFatal);
    assert(tracker.invalid());
    assert(!tracker.gap_pending());

    tracker.reset();
    assert(tracker.observe(3000) == luv::SequenceResult::kFirst);
    assert(tracker.observe(3001) == luv::SequenceResult::kNext);
    assert(tracker.observe(3001) == luv::SequenceResult::kDuplicate);
    assert(tracker.next() == 3002);

    tracker.reset();
    assert(tracker.observe(4000) == luv::SequenceResult::kFirst);
    assert(tracker.observe(4011) == luv::SequenceResult::kGap);
    assert(tracker.acknowledge_retransmit(4001, 4010));
    assert(!tracker.gap_pending());
    assert(tracker.gap_class() == luv::SequenceGapClass::kNone);

    std::puts("SequenceTracker recoverable/fatal gap, duplicate, replay, and reset checks passed.");
    return 0;
}