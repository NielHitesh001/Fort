#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

#include "luv_recovery.hpp"

int main() {
    const char* path = "/tmp/luv-recovery-process-test.bin";
    ::unlink(path);

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        luv::RecoveryLedger ledger;
        if (!ledger.open(path)) _exit(10);
        if (!ledger.append(luv::RecoveryEventType::kAdd, 100, 80, 1'000'000))
            _exit(11);
        if (!ledger.append(luv::RecoveryEventType::kFill, 100, 20))
            _exit(12);
        ::kill(::getpid(), SIGKILL);
        _exit(13);
    }

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGKILL);

    luv::RecoveryLedger ledger;
    assert(ledger.open(path));
    luv::RecoveredOrder orders[2]{};
    uint32_t count = 0;
    assert(ledger.replay(orders, 2, count));
    assert(count == 1);
    assert(orders[0].order_id == 100);
    assert(orders[0].remaining == 60);
    ::unlink(path);

    std::printf("process crash recovery passed: SIGKILL and replay\n");
    return 0;
}
