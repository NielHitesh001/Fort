'use client';

import React, { useState } from 'react';
import {
  ShieldCheck,
  ShieldAlert,
  Server,
  Cpu,
  HardDrive,
  Radio,
  Unplug,
  AlertTriangle,
  Layers,
  FileCode2,
  CheckCircle2,
  XCircle,
} from 'lucide-react';
import { StatusChip } from '@/components/primitives/StatusChip';
import { NumericValue } from '@/components/primitives/NumericValue';
import { EmptyState } from '@/components/primitives/EmptyState';
import { Button } from '@/components/ui/button';
import { MemoryPressureTiers, CircuitBreakerStates, CircuitBreakerState } from '@/lib/contracts';

interface SystemHealthScreenProps {
  healthOk?: boolean;
  wsConnected?: boolean;
  feedFresh?: boolean;
  halted?: boolean;
  metrics?: Record<string, number>;
}

export function SystemHealthScreen({
  healthOk = true,
  wsConnected = false,
  feedFresh = true,
  halted = false,
  metrics = {},
}: SystemHealthScreenProps) {
  // Arena memory utilization simulation / state
  const [arenaUsagePct, setArenaUsagePct] = useState<number>(34.2);
  const [circuitBreakerState, setCircuitBreakerState] = useState<CircuitBreakerState>(
    halted ? 'kOpen' : 'kClosed'
  );

  const activeOrders = metrics.luv_execution_active_orders ?? 0;
  const queueDepth = metrics.luv_telemetry_queue_depth ?? 0;

  // Determine Arena Pressure Tier
  let arenaTier: { name: string; threshold: string; status: 'ok' | 'warn' | 'critical' } =
    MemoryPressureTiers.kNormal;
  if (arenaUsagePct >= 95) {
    arenaTier = MemoryPressureTiers.kEmergencyHalt;
  } else if (arenaUsagePct >= 85) {
    arenaTier = MemoryPressureTiers.kShedLoad;
  } else if (arenaUsagePct >= 70) {
    arenaTier = MemoryPressureTiers.kWarning;
  }

  return (
    <div className="space-y-4">
      {/* STATUS GRID OF INDEPENDENT INDICATORS (Section 5.4) */}
      <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-6 gap-3">
        {/* 1. API Health */}
        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            API Health (/healthz)
          </span>
          <div className="mt-2">
            <StatusChip
              status={healthOk ? 'healthy' : 'unavailable'}
              label={healthOk ? '200 OK' : '503 Fail'}
            />
          </div>
          <span className="text-[9px] font-mono text-[var(--text-secondary)] block mt-1.5">
            Probe: :9090/healthz
          </span>
        </div>

        {/* 2. WebSocket Stream */}
        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            WebSocket Stream
          </span>
          <div className="mt-2">
            <StatusChip
              status={wsConnected ? 'connected' : 'reconnecting'}
              label={wsConnected ? 'Connected' : 'Disconnected'}
            />
          </div>
          <span className="text-[9px] font-mono text-[var(--text-secondary)] block mt-1.5">
            RFC 6455 :8080/stream
          </span>
        </div>

        {/* 3. Feed Freshness */}
        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Feed State
          </span>
          <div className="mt-2">
            <StatusChip
              status={feedFresh ? 'connected' : 'stalled'}
              label={feedFresh ? 'Ingesting' : 'Stalled (>100ms)'}
            />
          </div>
          <span className="text-[9px] font-mono text-[var(--text-secondary)] block mt-1.5">
            Contract: ITCH 5.0
          </span>
        </div>

        {/* 4. Arena Allocator */}
        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Arena Allocator
          </span>
          <div className="mt-2">
            <StatusChip
              status={arenaTier.status}
              label={`${arenaUsagePct.toFixed(1)}% (${arenaTier.name})`}
            />
          </div>
          <span className="text-[9px] font-mono text-[var(--text-secondary)] block mt-1.5">
            64MB Laptop Budget
          </span>
        </div>

        {/* 5. Circuit Breaker */}
        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Circuit Breaker
          </span>
          <div className="mt-2">
            <StatusChip
              status={circuitBreakerState === 'kClosed' ? 'live' : circuitBreakerState === 'kHalfOpen' ? 'warn' : 'critical'}
              label={circuitBreakerState}
            />
          </div>
          <span className="text-[9px] font-mono text-[var(--text-secondary)] block mt-1.5">
            luv_safety.hpp
          </span>
        </div>

        {/* 6. K8s Pod Status (Explicitly not wired per brief) */}
        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            K8s Pod Topology
          </span>
          <div className="mt-2">
            <StatusChip status="neutral" label="Not Wired" />
          </div>
          <span className="text-[9px] font-mono text-[var(--text-secondary)] block mt-1.5">
            Simulation Host Only
          </span>
        </div>
      </div>

      {/* Memory Arena Usage & Pressure Details (Section 5.4) */}
      <div className="panel-card">
        <div className="panel-header">
          <div className="flex items-center gap-2">
            <Cpu className="w-4 h-4 text-[var(--accent)]" />
            <h2>Zero-Allocation Memory Arena (luv_arena.hpp)</h2>
          </div>
          <span className="text-[11px] font-mono text-[var(--text-secondary)]">
            Total Infrastructure: ~844 MB · AI Region: 64 MB (13 GB optional)
          </span>
        </div>

        <div className="p-4 space-y-4">
          <div>
            <div className="flex justify-between text-xs font-mono mb-1.5">
              <span>Current Allocation Utilization</span>
              <span className="font-bold">{arenaUsagePct.toFixed(1)}% of slab capacity</span>
            </div>
            {/* Multi-tier progress bar */}
            <div className="w-full h-3 bg-[var(--bg-canvas)] rounded overflow-hidden flex border border-[var(--border-subtle)]">
              <div
                className={`h-full transition-all duration-300 ${
                  arenaUsagePct >= 95
                    ? 'bg-red-500'
                    : arenaUsagePct >= 85
                    ? 'bg-amber-500'
                    : arenaUsagePct >= 70
                    ? 'bg-yellow-500'
                    : 'bg-emerald-500'
                }`}
                style={{ width: `${Math.min(100, arenaUsagePct)}%` }}
              />
            </div>
            <div className="flex justify-between text-[10px] text-[var(--text-secondary)] font-mono mt-1">
              <span>0% (Normal)</span>
              <span className="text-yellow-400">70% (Warning)</span>
              <span className="text-amber-400">85% (Shed Load: Cancels Only)</span>
              <span className="text-red-400 font-semibold">95% (Emergency Halt)</span>
            </div>
          </div>

          <div className="grid grid-cols-1 md:grid-cols-4 gap-3 pt-2 border-t border-[var(--border-subtle)] text-xs font-mono">
            <div className="p-2.5 rounded bg-[var(--bg-surface-2)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">LOB SLAB</span>
              <span className="font-semibold">576 MB</span>
              <p className="text-[10px] text-[var(--text-secondary)] mt-0.5">512 symbols × 2 × 1024 levels</p>
            </div>
            <div className="p-2.5 rounded bg-[var(--bg-surface-2)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">TICK RING</span>
              <span className="font-semibold">256 MB</span>
              <p className="text-[10px] text-[var(--text-secondary)] mt-0.5">4,194,304 TickMsg slots</p>
            </div>
            <div className="p-2.5 rounded bg-[var(--bg-surface-2)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">TELEMETRY SPSC</span>
              <span className="font-semibold">8 MB</span>
              <p className="text-[10px] text-[var(--text-secondary)] mt-0.5">65,536 snapshots ring</p>
            </div>
            <div className="p-2.5 rounded bg-[var(--bg-surface-2)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">AI MODEL REGION</span>
              <span className="font-semibold">64 MB (Config)</span>
              <p className="text-[10px] text-[var(--text-secondary)] mt-0.5">Treelite / TL2cgen cache</p>
            </div>
          </div>
        </div>
      </div>

      {/* Circuit Breaker & Safety State Machine (Section 5.4) */}
      <div className="panel-card">
        <div className="panel-header">
          <div className="flex items-center gap-2">
            <ShieldAlert className="w-4 h-4 text-amber-400" />
            <h2>Circuit Breaker & Kill Switch (luv_safety.hpp)</h2>
          </div>
          <span className="text-[11px] font-mono text-[var(--text-secondary)]">
            Read-only Gateway State Machine
          </span>
        </div>

        <div className="p-4 space-y-4">
          <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
            <div
              className={`p-3 rounded border font-mono ${
                circuitBreakerState === 'kClosed'
                  ? 'border-[var(--status-ok)] bg-[var(--status-ok)]/10 text-[var(--status-ok)]'
                  : 'border-[var(--border-subtle)] bg-[var(--bg-surface-2)] text-[var(--text-secondary)] opacity-60'
              }`}
            >
              <div className="flex items-center gap-2">
                <CheckCircle2 className="w-4 h-4" />
                <strong className="text-xs">kClosed (Normal)</strong>
              </div>
              <p className="text-[11px] mt-1">
                Zero rejections. All ingress order intents are admitted to PreTradeRisk.
              </p>
            </div>

            <div
              className={`p-3 rounded border font-mono ${
                circuitBreakerState === 'kHalfOpen'
                  ? 'border-[var(--status-warn)] bg-[var(--status-warn)]/10 text-[var(--status-warn)]'
                  : 'border-[var(--border-subtle)] bg-[var(--bg-surface-2)] text-[var(--text-secondary)] opacity-60'
              }`}
            >
              <div className="flex items-center gap-2">
                <AlertTriangle className="w-4 h-4" />
                <strong className="text-xs">kHalfOpen (Probing)</strong>
              </div>
              <p className="text-[11px] mt-1">
                Cool-down expired. Single probe order issued to test engine recovery.
              </p>
            </div>

            <div
              className={`p-3 rounded border font-mono ${
                circuitBreakerState === 'kOpen'
                  ? 'border-[var(--status-critical)] bg-[var(--status-critical)]/10 text-[var(--status-critical)]'
                  : 'border-[var(--border-subtle)] bg-[var(--bg-surface-2)] text-[var(--text-secondary)] opacity-60'
              }`}
            >
              <div className="flex items-center gap-2">
                <XCircle className="w-4 h-4" />
                <strong className="text-xs">kOpen (Tripped)</strong>
              </div>
              <p className="text-[11px] mt-1">
                Consecutive rejections or memory limit tripped. All new orders rejected.
              </p>
            </div>
          </div>

          <div className="p-3 bg-[var(--bg-surface-2)] rounded border border-[var(--border-subtle)] text-xs text-[var(--text-secondary)] flex items-center justify-between flex-wrap gap-2">
            <span>
              Manual Trip Endpoint: <code className="font-mono text-[var(--text-primary)]">Read-Only</code> (No public trip route exposed by local HTTP server)
            </span>
            <div className="flex items-center gap-2">
              <Button
                size="sm"
                variant="outline"
                className="text-[11px] h-7"
                onClick={() =>
                  setCircuitBreakerState(
                    circuitBreakerState === 'kClosed' ? 'kOpen' : 'kClosed'
                  )
                }
              >
                Simulate Breaker State Toggle
              </Button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
