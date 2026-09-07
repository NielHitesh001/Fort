# LUV---Flicker- Pilot Regulatory Package

Date: 2026-09-07
Repository: `NielHitesh001/Arbor`
Engine: `Arbor / LUV Flicker`

## Executive Summary

This memo documents the non-custodial data-vendor model for LUV---Flicker- and the regulatory posture to present to a compliance attorney and pilot customer.

The framework provides real-time order-book reconstruction, ITCH decode, audit-chain continuity, and execution telemetry. It does not hold customer capital, clear trades, or settle securities. The relevant compliance posture is therefore most consistent with a non-custodial data vendor / analytics provider model, contingent on the final commercial structure and the attorney's review of any customer-specific routing or agency arrangements.

## Business Model

- Input: live ITCH TotalView-style feed and internal symbol metadata
- Processing: decode, validate, reconstruct LOB, generate analytics, emit audit records
- Output: real-time trading data and audit trail to the customer's own infrastructure
- Revenue: SaaS subscription / pilot fee / support engagement
- Customer role: retains capital, executes with its own broker, and owns its operational decisions

## What We Do Not Do

- Do not hold customer funds
- Do not custody securities
- Do not settle or clear transactions
- Do not execute trades on behalf of customers
- Do not provide prime-broker, custodial, or financing services

## Why the Regulatory Framing Is Reasonable

The core platform is a data-and-audit primitive layered on top of market data ingestion. The product is best described as a real-time deep-book analytics and compliance layer. The customer remains responsible for its own execution decisions and all legal obligations tied to those decisions.

The codebase already encodes the relevant guardrails:

- durable append-only audit log with hash chaining
- restart-continuity checks
- execution admission controls and symbol-level risk checks
- telemetry and reconciliation primitives for operational validation

This is materially different from operating a broker-dealer or custody workflow.

## Attorney Intake Request

Subject: Compliance opinion for non-custodial data vendor

Hi [Attorney],

We're building a non-custodial real-time market-data terminal for institutional traders. We provide live ITCH feed processing, order-book reconstruction, and a durable audit trail, but we do not hold customer capital, clear trades, or execute on behalf of customers.

We need an opinion on:

1. Whether the structure qualifies as a non-custodial data vendor / analytics provider rather than a broker-dealer or MSB under the relevant U.S. framework.
2. Whether the current audit model satisfies the spirit of FINRA Rule 4512 recording requirements for order and execution records.
3. Whether our pilot DPA/SEA terms need specific data-residency, audit, liability, and customer-data clauses.
4. Whether any customer-specific routing or execution arrangement would change the classification.

We would be grateful for a short feasibility review and a quote for an opinion letter or contract review package.

Thanks,
Niel

## Compliance Questions to Ask the Attorney

1. Does our current model require MSB licensing or broker-dealer registration?
2. Do we need a dedicated DPA/SEA structure for pilot customers?
3. What audit data should be retained and exported for customer review?
4. What legal language should we include to avoid being treated as a broker or execution agent?
5. What is the minimum viable compliance posture for a 2-week pilot?

## Proposed DPA/SEA Pilot Clauses

- The customer owns its data and trade records.
- The provider does not hold customer funds or securities.
- The provider only processes data in the customer's permitted environment or a hosted environment under explicit customer authorization.
- The provider offers local tamper-evident audit records and export access, subject to agreed retention, backup, access-control, and customer review requirements.
- The provider's liability for pilot services is capped at a low pilot-phase amount such as $10,000.
- The customer retains responsibility for trading decisions and external regulatory obligations.
- The provider is not a broker, custodian, or exchange agent unless separately contracted and licensed.

## Evidence to Share with Counsel

- SHA-256 chained durable audit records
- execution risk and reconciliation logic
- restart continuity and recovery validation
- telemetry and drop-count reporting
- proposed pilot structure and SLA model

This package is a legal-intake document and not a legal opinion. Counsel review is still required before reliance.
