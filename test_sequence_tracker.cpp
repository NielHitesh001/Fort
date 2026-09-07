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

    std::puts("SequenceTracker gap, duplicate, ordering, and reset checks passed.");
    return 0;
}