#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

#include "luv_safety.hpp"

int main() {
    const char* path = "/tmp/luv-audit-integrity-test.bin";
    const char* manifest_path = "/tmp/luv-audit-manifest-test.bin";
    ::unlink(path);
    ::unlink(manifest_path);

    {
        luv::DurableAuditLog log;
        assert(log.open(path, 1));
        assert(log.append(100, 10, 1'000'000, 10, 3, 0, 'A'));
        assert(log.append(200, 11, 1'010'000, 5, 3, 1, 'A'));
        assert(log.append(300, 12, 1'020'000, 8, 3, 0, 'U'));
        assert(log.size() == 3);

        // Write manifest checkpoint for range [0..2]
        assert(log.write_manifest_checkpoint(manifest_path, 0, 2));
    }

    // Verify checkpoint manifest matches log file
    assert(luv::DurableAuditLog::verify_manifest(path, manifest_path));

    // Test tamper detection with corrupted log
    int fd = ::open(path, O_RDWR);
    assert(fd >= 0);
    uint8_t corrupted = 0;
    assert(::pread(fd, &corrupted, 1, 16) == 1);
    corrupted ^= 0x01;
    assert(::pwrite(fd, &corrupted, 1, 16) == 1);
    ::close(fd);

    // Manifest verification must fail after tamper
    assert(!luv::DurableAuditLog::verify_manifest(path, manifest_path));

    luv::DurableAuditLog rejected;
    assert(!rejected.open(path));
    ::unlink(path);
    ::unlink(manifest_path);

    std::printf("audit integrity passed: chain corruption rejected and manifest verified\n");
    return 0;
}
