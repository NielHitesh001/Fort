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

    // A hash chain validates the history it receives. Without an independently
    // retained checkpoint, removing the final complete record is undetectable;
    // this is intentionally documented as a simulation-only limitation.
    const char* truncated_path = "/tmp/luv-audit-truncation-test.bin";
    ::unlink(truncated_path);
    {
        luv::DurableAuditLog log;
        assert(log.open(truncated_path, 1));
        assert(log.append(100, 10, 1'000'000, 10, 3, 0, 'A'));
        assert(log.append(200, 11, 1'010'000, 5, 3, 1, 'A'));
        assert(log.append(300, 12, 1'020'000, 8, 3, 0, 'U'));
    }
    fd = ::open(truncated_path, O_RDWR);
    assert(fd >= 0);
    assert(::ftruncate(fd, 2 * static_cast<off_t>(sizeof(luv::AuditEvent))) == 0);
    ::close(fd);
    uint64_t valid_events = 0;
    assert(luv::DurableAuditLog::verify_log_integrity(truncated_path, valid_events));
    assert(valid_events == 2);
    ::unlink(truncated_path);

    std::printf("audit integrity passed: chain corruption rejected and manifest verified\n");
    return 0;
}
