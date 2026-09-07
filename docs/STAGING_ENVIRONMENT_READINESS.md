# Staging Environment Readiness

Status: **PENDING VERIFICATION**
Date: 2026-09-07

This checklist records external prerequisites. Do not mark an item complete
without an operator-confirmed result and supporting detail.

## AWS Infrastructure

- [ ] AWS `t3.medium` instance is running
- [ ] Instance ID recorded: `________________`
- [ ] SSH access confirmed
- [ ] Required security-group access confirmed

## OUCH Replay Feed

- [ ] OUCH replay endpoint recorded: `________________`
- [ ] Replay feed is accessible from staging
- [ ] NASDAQ ITCH sample is available
- [ ] Replay harness processed at least 5 messages

## Prometheus and Grafana

- [ ] Prometheus is running on staging
- [ ] Grafana dashboard is configured
- [ ] Application scrape target is reachable
- [ ] Latency, memory, and reconciliation alerts are configured

## Pre-Deployment Approval

- [ ] All prerequisites above verified
- [ ] Verified by: `________________`
- [ ] Verification timestamp (UTC): `________________`
- [ ] Evidence links or command output: `________________`

Gate A remains pending until this checklist is completed and the verification
script succeeds.
