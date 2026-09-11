# Security & Safety Policy

## What Fort is not

Fort is educational simulation and research software. It is not a regulated broker-dealer, clearing member, trading venue, custody system, exchange-certified gateway, or regulatory-reporting service. Its OUCH/FIX components are simulation-oriented and are not live exchange integrations.

The regulatory and surveillance modules implement models and scenarios for research. Their presence does not constitute SEC, FINRA, CFTC, CAT, or any other regulatory compliance certification.

## Audit-trail limitations

`DurableAuditLog` provides local SHA-256 hash chaining. It can detect changes to records within the history it is given, and an externally preserved manifest can anchor a specific range. It does not provide immutable storage, independent custody, trusted timestamps, retention enforcement, or proof that earlier history was not removed and re-created.

An actor with filesystem access can truncate a log and its local checkpoint or produce a new valid chain. Do not submit these records to a regulator or represent them as compliant with SEC Rule 17a-4, FINRA recordkeeping rules, or CAT requirements.

## Threat model

In scope: malformed input rejection, bounded in-memory structures, and detection of accidental local corruption in a supplied audit file.

Out of scope: an adversary controlling the host or filesystem, live exchange authentication, DDoS protection, customer credential management, financial custody, regulatory retention, and compliance certification.

## A compliant deployment requires

- Independently administered WORM or immutable storage with retention and legal-hold controls.
- External custody and regulator-access arrangements appropriate to the applicable rule set.
- Trusted timestamping and protected signing keys, typically through a managed service or HSM-backed system.
- Production exchange connectivity, reconciliation, operational controls, and independent legal/compliance review.

## Security design practices

- Critical paths use pre-allocated memory to limit allocation pressure and fragmentation.
- Supported ITCH messages are length- and range-validated before decoding.
- The project supports ASan and UBSan builds; passing tests increase confidence but are not a guarantee of absence of defects.

## Testing with LLVM Sanitizers

```bash
cmake -S . -B build-asan -DLUV_ENABLE_ASAN_UBSAN=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## Reporting vulnerabilities

If you discover a security vulnerability or bug within this educational framework, please open an issue on GitHub or submit a pull request with regression tests.
