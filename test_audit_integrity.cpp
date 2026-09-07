#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

#include "luv_safety.hpp"

int main() {
    const char* path = "/tmp/luv-audit-integrity-test.bin";
    ::unlink(path);

    {
        luv::DurableAuditLog log;
        assert(log.open(path, 1));
        assert(log.append(1, 10, 1'000'000, 10, 3, 0, 'A'));
        assert(log.append(2, 11, 1'010'000, 5, 3, 1, 'A'));
        assert(log.size() == 2);
    }

    int fd = ::open(path, O_RDWR);
    assert(fd >= 0);
    uint8_t corrupted = 0;
    assert(::pread(fd, &corrupted, 1, 16) == 1);
    corrupted ^= 0x01;
    assert(::pwrite(fd, &corrupted, 1, 16) == 1);
    ::close(fd);

    luv::DurableAuditLog rejected;
    assert(!rejected.open(path));
    ::unlink(path);

    std::printf("audit integrity passed: chain corruption rejected\n");
    return 0;
}
