# Pilot DPA / SEA Template

## 1. Parties

- Provider: LUV---Flicker- / [Company Legal Name]
- Customer: [Customer Name]
- Pilot Duration: 14 days
- Start Date: [Date]

## 2. Scope of Services

The Provider will provide:

- live market-data processing and order-book reconstruction
- audit-trail generation and export
- latency and integrity telemetry
- support during the pilot window

The Customer will retain responsibility for:

- data access rights and market data license compliance
- its own execution decisions
- trade routing, settlement, and compliance obligations
- its own capital and clearing arrangements

## 3. Data Protection and Audit Terms

- Customer retains ownership of its operational data.
- Provider will not hold customer funds or securities.
- Provider will store only the minimum operational data required to maintain the pilot.
- Customer may export audit records and compliance reports in a structured format.
- Provider will preserve append-only audit logs and hash-chain continuity evidence.

## 4. Service Levels

- Availability target: 99.5% during pilot
- p99 latency target: under 1ms for decode-to-LOB update pipeline on supported hardware
- Order accuracy target: 100% relative to validated feed logic
- Audit continuity target: 100% record integrity for retained log entries

## 5. Payment

- Pilot fee: $5,000 to $10,000 for a 2-week engagement
- Includes onboarding, setup, and daily review
- Additional support billed separately if required

## 6. Termination and Data Return

- Either party may terminate on 3 days' written notice
- Customer retains all trade and audit data generated under the pilot
- Provider deletes operational logs after the retention window unless customer requests retention

## 7. Liability and Risk Allocation

- Provider is not a custodian, broker, or execution agent under this pilot
- Customer assumes the risk of trading decisions and operational execution choices
- Provider liability for pilot services is capped at a value suitable for a pilot engagement, such as $10,000
- Provider is not responsible for trading losses or customer-specific execution risk

## 8. Legal Review Notes

This draft is a starting point and must be reviewed by counsel before use. The final agreement should be tailored to the customer, jurisdiction, and any exchange or market-data license terms.
