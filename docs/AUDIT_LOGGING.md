# Audit logging: local integrity only

Fort's audit components are simulation primitives, not a regulatory recordkeeping solution.

`DurableAuditLog` in `luv_safety.hpp` writes append-only local records linked by SHA-256 hashes. `verify_log_integrity` detects an altered, reordered, or malformed record sequence that is presented to it. A manifest checkpoint can anchor a particular known range when the checkpoint is independently preserved.

## What it does not establish

The implementation does not supply WORM storage, immutable retention, external custody, trusted timestamps, protected signing keys, legal holds, regulator access, or proof of complete history. A party controlling both the filesystem and locally stored checkpoints can truncate or replace a chain and create a new internally consistent history.

Consequently, Fort audit records must not be represented as satisfying SEC Rule 17a-4, FINRA recordkeeping requirements, CAT reporting requirements, or any other regulated retention obligation.

## Appropriate use

Use the audit code to study append-only event formats, local corruption detection, recovery workflows, and test fixtures. For a regulated deployment, integrate an independently administered retention and custody provider, apply the controls required by the governing regulation, and obtain legal and compliance approval.

See [SECURITY.md](../SECURITY.md) for the threat model and [COMPLIANCE_STATUS.md](COMPLIANCE_STATUS.md) for the feature-level scope.
