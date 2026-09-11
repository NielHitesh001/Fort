# Troubleshooting

Fort emits structured error events only when an application registers a `luv::StructuredLogger` sink. Events are allocation-free and use static strings so the caller can forward them to its own telemetry or logging backend.

| Module / event | Meaning | Operator response |
| --- | --- | --- |
| `decode/message_too_short` | A handled ITCH message was shorter than its minimum wire size. | Drop the message, inspect feed framing, and monitor for recurrence. |
| `decode/null_input` | The decoder was called without a buffer. | Correct the caller; no message was decoded. |
| `lob/hash_table_high_load` | The order-reference map reached its configured safe occupancy limit. | Stop or resynchronize the affected simulation; do not treat its book as complete. |
| `lob/hash_table_probe_limit` | An order-reference lookup/insert exceeded the bounded collision-probe budget. | Investigate malformed or adversarial order references and resynchronize. |
| `risk/invalid_limits` | A configured risk limit is inconsistent or cannot be represented by the outbound wire format. | Reject the configuration and correct the values before submitting orders. |

The absence of a registered sink does not change rejection behavior: malformed inputs and invalid configurations are still rejected and their module counters continue to update.
