#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "luv_recovery.hpp"

int main() {
    const char* path = "/tmp/luv-recovery-test.bin";
    ::unlink(path);

    {
        luv::RecoveryLedger ledger;
        assert(ledger.open(path));
        assert(ledger.append_with_side(luv::RecoveryEventType::kAdd, 10, 100,
                           1'000'000, 1, 111));
        assert(ledger.append(luv::RecoveryEventType::kFill, 10, 40));
        assert(ledger.append(luv::RecoveryEventType::kAdd, 11, 50, 1'010'000));
        assert(ledger.append(luv::RecoveryEventType::kCancel, 11, 50));
        assert(ledger.size() == 4);
    }

    {
        luv::RecoveryLedger ledger;
        assert(ledger.open(path));
        luv::RecoveredOrder orders[4]{};
        uint32_t count = 0;
        assert(ledger.replay(orders, 4, count));
        assert(count == 1);
        assert(orders[0].order_id == 10);
        assert(orders[0].remaining == 60);
        assert(orders[0].price == 1'000'000);
        assert(orders[0].side == 1);
        assert(orders[0].timestamp_ns == 111);
    }

    int fd = ::open(path, O_WRONLY | O_APPEND);
    assert(fd >= 0);
    const uint8_t corrupt = 0xFF;
    assert(::write(fd, &corrupt, sizeof(corrupt)) == sizeof(corrupt));
    ::close(fd);

    luv::RecoveryLedger corrupted;
    assert(!corrupted.open(path));
    corrupted.close();
    ::unlink(path);

    std::printf("crash recovery replay passed: restart and corruption checks\n");
    return 0;
}
