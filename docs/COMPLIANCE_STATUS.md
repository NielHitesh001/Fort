# Compliance-Status Matrix (Parked Research State)

> [!IMPORTANT]
> **Parked Research Software:** All items below are simulation or research prototypes. “Implemented” means that Fort contains academic code modeling a workflow or calculation; it does NOT mean certified, legally compliant, approved by regulators, or suitable for live production use.

| Feature | Implemented scope | Status |
| --- | --- | --- |
| SEC Rule 15c3-1 net-capital calculations | Mathematical model and tests | RESEARCH_ONLY |
| SEC Rule 15c3-3 customer-protection calculations | Mathematical model and tests | RESEARCH_ONLY |
| FINRA Rule 4210 margin logic | Simulation model | RESEARCH_ONLY |
| SEC Rule 201 / Regulation NMS controls | Simulation model | RESEARCH_ONLY |
| CFTC and ESMA surveillance rules | Pattern-detection simulation | RESEARCH_ONLY |
| CAT reporting | Stub/modelled reporting paths | SIMULATION_ONLY |
| ITCH decoding and order-book reconstruction | Local simulation input processing | SIMULATION_ONLY |
| OUCH/FIX execution interfaces | Packet and workflow simulation | NOT_LIVE_EXCHANGE_CERTIFIED |
| Durable audit log | Local SHA-256 hash chain | NOT_REGULATORY_COMPLIANT |
| WORM retention, custody, legal hold, trusted timestamping | Not provided | EXCLUDED |

For the audit threat model and limitations, see [SECURITY.md](../SECURITY.md) and [AUDIT_LOGGING.md](AUDIT_LOGGING.md).
