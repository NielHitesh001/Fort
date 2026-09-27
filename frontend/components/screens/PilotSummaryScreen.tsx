'use client';

import React from 'react';
import Link from 'next/link';
import {
  ShieldCheck,
  CheckCircle2,
  XCircle,
  Clock,
  ArrowUpRight,
  FileCode2,
  ExternalLink,
  Layers,
  Cpu,
  Lock,
  Scale,
  Zap,
} from 'lucide-react';
import { StatusChip } from '@/components/primitives/StatusChip';
import { NumericValue } from '@/components/primitives/NumericValue';
import { SandboxWatermark } from '@/components/primitives/SandboxWatermark';
import { Button } from '@/components/ui/button';

interface PilotSummaryScreenProps {
  metrics: Record<string, number>;
  fresh: boolean;
  healthOk: boolean;
}

export function PilotSummaryScreen({
  metrics,
  fresh,
  healthOk,
}: PilotSummaryScreenProps) {
  const tickRate = metrics.luv_execution_tick_rate_hz;
  const fillsTotal = metrics.luv_execution_fills_total;
  const riskNs = metrics.luv_execution_risk_check_latency_nanoseconds;
  const activeOrders = metrics.luv_execution_active_orders;

  return (
    <div className="space-y-4">
      {/* Verified Headline Metrics Grid (Sourced 1:1 from Telemetry, No Fabrication) */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
        <div className="panel-card p-4">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Observed Throughput
          </span>
          <div className="text-2xl font-mono font-bold text-[var(--status-ok)] my-1">
            <NumericValue value={tickRate} unit="events/s" />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            Prometheus gauge (port 9090)
          </span>
        </div>

        <div className="panel-card p-4">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Risk Gate Latency
          </span>
          <div className="text-2xl font-mono font-bold text-[var(--accent)] my-1">
            <NumericValue value={riskNs} unit="ns" precision={1} />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            Single-pass pre-trade check
          </span>
        </div>

        <div className="panel-card p-4">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Reconciled Fills
          </span>
          <div className="text-2xl font-mono font-bold text-[var(--text-primary)] my-1">
            <NumericValue value={fillsTotal} unit="fills" />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            Monotonic process counter
          </span>
        </div>

        <div className="panel-card p-4">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Active Order Slots
          </span>
          <div className="text-2xl font-mono font-bold text-[var(--text-primary)] my-1">
            <NumericValue value={activeOrders} unit="orders" />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            Current tracked memory slots
          </span>
        </div>
      </div>

      {/* EXPLICIT MATURITY STATEMENT (Section 5.6 & INVESTOR_READINESS_BRIEF.md) */}
      <div className="panel-card p-5 border-l-4 border-l-blue-500">
        <div className="flex items-center gap-2 mb-2">
          <span className="text-xs font-mono font-bold text-blue-400 uppercase tracking-wider">
            SYSTEM MATURITY & OPERATIONAL STATUS (docs/PARKED.md & docs/CAPABILITIES.md)
          </span>
        </div>
        <h2 className="text-lg font-semibold text-[var(--text-primary)] mb-2">
          Research-Grade Microstructure Simulator (Status: PARKED)
        </h2>
        <p className="text-xs text-[var(--text-secondary)] leading-relaxed max-w-4xl">
          Fort (product name &quot;Corridor&quot;) is a high-performance in-process C++20 limit order book simulator designed for market microstructure research, algorithmic execution benchmarking, and systems analysis. It is <strong>NOT</strong> a licensed broker-dealer, live exchange gateway, DMA session, or certified regulatory filing platform. It does not connect to live exchange sessions or trade capital.
        </p>
      </div>

      {/* PROVEN VS UNPROVEN / OUT-OF-SCOPE MATRIX */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {/* Validated Capabilities */}
        <div className="panel-card">
          <div className="panel-header">
            <div className="flex items-center gap-2">
              <CheckCircle2 className="w-4 h-4 text-[var(--status-ok)]" />
              <h2>Validated Architectural Capabilities</h2>
            </div>
            <StatusChip status="live" label="Proven In Engine" />
          </div>
          <div className="p-4 space-y-3 text-xs">
            <div className="flex items-start gap-2.5">
              <CheckCircle2 className="w-4 h-4 text-[var(--status-ok)] flex-shrink-0 mt-0.5" />
              <div>
                <strong className="text-[var(--text-primary)]">Nasdaq ITCH 5.0 Binary Parsing:</strong>
                <p className="text-[var(--text-secondary)] text-[11px]">
                  Bounds-checked zero-allocation parser for System Event, Stock Directory, Add Order, Trade, and NOII.
                </p>
              </div>
            </div>

            <div className="flex items-start gap-2.5">
              <CheckCircle2 className="w-4 h-4 text-[var(--status-ok)] flex-shrink-0 mt-0.5" />
              <div>
                <strong className="text-[var(--text-primary)]">In-Memory Price-Time FIFO LOB:</strong>
                <p className="text-[var(--text-secondary)] text-[11px]">
                  512 symbols, 1024 price levels/side, cache-aligned matching engine in contiguous memory.
                </p>
              </div>
            </div>

            <div className="flex items-start gap-2.5">
              <CheckCircle2 className="w-4 h-4 text-[var(--status-ok)] flex-shrink-0 mt-0.5" />
              <div>
                <strong className="text-[var(--text-primary)]">Zero-Allocation Memory Arena:</strong>
                <p className="text-[var(--text-secondary)] text-[11px]">
                  Contiguous mmap slab (luv_arena.hpp) eliminating dynamic heap allocations on critical hot path.
                </p>
              </div>
            </div>

            <div className="flex items-start gap-2.5">
              <CheckCircle2 className="w-4 h-4 text-[var(--status-ok)] flex-shrink-0 mt-0.5" />
              <div>
                <strong className="text-[var(--text-primary)]">Pre-Trade Risk Gateway:</strong>
                <p className="text-[var(--text-secondary)] text-[11px]">
                  Position caps, collar limits, consecutive rejection breakers, and sub-microsecond validation.
                </p>
              </div>
            </div>
          </div>
        </div>

        {/* Known Limitations & Excluded Components */}
        <div className="panel-card">
          <div className="panel-header">
            <div className="flex items-center gap-2">
              <XCircle className="w-4 h-4 text-[var(--status-critical)]" />
              <h2>Explicitly Excluded / Out-of-Scope Components</h2>
            </div>
            <StatusChip status="critical" label="Not In Product" />
          </div>
          <div className="p-4 space-y-3 text-xs">
            <div className="flex items-start gap-2.5">
              <XCircle className="w-4 h-4 text-[var(--status-critical)] flex-shrink-0 mt-0.5" />
              <div>
                <strong className="text-[var(--text-primary)]">Live Multicast & DMA Exchange Feeds:</strong>
                <p className="text-[var(--text-secondary)] text-[11px]">
                  Requires direct exchange licensing (Nasdaq/Cboe/CME); local simulator uses synthetic replay.
                </p>
              </div>
            </div>

            <div className="flex items-start gap-2.5">
              <XCircle className="w-4 h-4 text-[var(--status-critical)] flex-shrink-0 mt-0.5" />
              <div>
                <strong className="text-[var(--text-primary)]">Physical Custody, Clearing & Settlement:</strong>
                <p className="text-[var(--text-secondary)] text-[11px]">
                  No DTCC/NSCC/Euroclear clearinghouse integration or multi-currency fiat banking rails.
                </p>
              </div>
            </div>

            <div className="flex items-start gap-2.5">
              <XCircle className="w-4 h-4 text-[var(--status-critical)] flex-shrink-0 mt-0.5" />
              <div>
                <strong className="text-[var(--text-primary)]">Statutory AML/KYC & FinCEN SAR Filing:</strong>
                <p className="text-[var(--text-secondary)] text-[11px]">
                  No statutory customer onboarding or automated compliance filing workflows.
                </p>
              </div>
            </div>

            <div className="flex items-start gap-2.5">
              <XCircle className="w-4 h-4 text-[var(--status-critical)] flex-shrink-0 mt-0.5" />
              <div>
                <strong className="text-[var(--text-primary)]">Multi-Region Raft Clustering & Failover:</strong>
                <p className="text-[var(--text-secondary)] text-[11px]">
                  Single-machine deployment target; multi-region headers are academic research models only.
                </p>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Pilot Discovery Quick Navigation */}
      <div className="panel-card p-4">
        <h3 className="text-xs font-mono font-bold text-[var(--text-primary)] uppercase mb-3">
          Explore Operational Subsystems
        </h3>
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-3 text-xs">
          <Link
            href="/book"
            className="p-3 rounded border border-[var(--border-subtle)] bg-[var(--bg-surface-2)] hover:border-[var(--accent)] transition-state flex items-center justify-between"
          >
            <div>
              <strong className="block text-[var(--text-primary)]">Order Book Ladder</strong>
              <span className="text-[11px] text-[var(--text-secondary)]">Inspect in-memory LOB depth</span>
            </div>
            <ArrowUpRight className="w-4 h-4 text-[var(--accent)]" />
          </Link>

          <Link
            href="/execution"
            className="p-3 rounded border border-[var(--border-subtle)] bg-[var(--bg-surface-2)] hover:border-[var(--accent)] transition-state flex items-center justify-between"
          >
            <div>
              <strong className="block text-[var(--text-primary)]">Execution Gateway</strong>
              <span className="text-[11px] text-[var(--text-secondary)]">Submit & monitor test orders</span>
            </div>
            <ArrowUpRight className="w-4 h-4 text-[var(--accent)]" />
          </Link>

          <Link
            href="/telemetry"
            className="p-3 rounded border border-[var(--border-subtle)] bg-[var(--bg-surface-2)] hover:border-[var(--accent)] transition-state flex items-center justify-between"
          >
            <div>
              <strong className="block text-[var(--text-primary)]">Prometheus Telemetry</strong>
              <span className="text-[11px] text-[var(--text-secondary)]">Inspect real-time metrics</span>
            </div>
            <ArrowUpRight className="w-4 h-4 text-[var(--accent)]" />
          </Link>
        </div>
      </div>
    </div>
  );
}
