'use client';

import React from 'react';
import {
  Activity,
  Zap,
  Gauge,
  AlertTriangle,
  Radio,
  FileCode2,
  TrendingDown,
  Layers,
} from 'lucide-react';
import {
  ResponsiveContainer,
  CartesianGrid,
  XAxis,
  YAxis,
  Tooltip,
  Line,
  LineChart,
  AreaChart,
  Area,
  BarChart,
  Bar,
} from 'recharts';
import { NumericValue } from '@/components/primitives/NumericValue';
import { StatusChip } from '@/components/primitives/StatusChip';
import { EmptyState } from '@/components/primitives/EmptyState';
import { Sample } from '@/lib/connection';

interface TelemetryScreenProps {
  metrics: Record<string, number>;
  history: Sample[];
  fresh: boolean;
  age: number | null;
}

export function TelemetryScreen({ metrics, history, fresh, age }: TelemetryScreenProps) {
  const tickRate = fresh ? metrics.luv_execution_tick_rate_hz : undefined;
  const riskNs = fresh ? metrics.luv_execution_risk_check_latency_nanoseconds : undefined;
  const inferenceUs = fresh ? metrics.luv_execution_inference_latency_microseconds : undefined;
  const fillsTotal = fresh ? metrics.luv_execution_fills_total : undefined;
  const rejectionsTotal = fresh ? metrics.luv_execution_rejections_total : undefined;
  const wsDrops = fresh ? metrics.luv_websocket_fill_notification_drops_total : undefined;
  const queueDepth = fresh ? metrics.luv_telemetry_queue_depth : undefined;
  const droppedSnapshots = fresh ? metrics.luv_telemetry_dropped_snapshots_total : undefined;

  return (
    <div className="space-y-4">
      {/* HEADLINE STAT ROW (Display-scale typography, max 3-4 figures, Section 5.3) */}
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-3">
        <div className="panel-card p-4">
          <div className="flex items-center justify-between">
            <span className="text-[11px] font-mono text-[var(--text-secondary)] uppercase">
              Tick Ingestion Rate
            </span>
            <Activity className="w-4 h-4 text-[var(--status-ok)]" />
          </div>
          <div className="text-2xl lg:text-3xl font-mono font-bold text-[var(--text-primary)] my-2">
            <NumericValue value={tickRate} unit="events/s" precision={0} />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            Metric: <code className="font-mono text-[var(--text-primary)]">luv_execution_tick_rate_hz</code>
          </span>
        </div>

        <div className="panel-card p-4">
          <div className="flex items-center justify-between">
            <span className="text-[11px] font-mono text-[var(--text-secondary)] uppercase">
              Last Risk Check Latency
            </span>
            <Zap className="w-4 h-4 text-[var(--accent)]" />
          </div>
          <div className="text-2xl lg:text-3xl font-mono font-bold text-[var(--accent)] my-2">
            <NumericValue value={riskNs} unit="ns" precision={1} />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            Metric: <code className="font-mono text-[var(--text-primary)]">luv_execution_risk_check_latency_nanoseconds</code>
          </span>
        </div>

        <div className="panel-card p-4">
          <div className="flex items-center justify-between">
            <span className="text-[11px] font-mono text-[var(--text-secondary)] uppercase">
              Last Inference Latency
            </span>
            <Gauge className="w-4 h-4 text-purple-400" />
          </div>
          <div className="text-2xl lg:text-3xl font-mono font-bold text-purple-300 my-2">
            <NumericValue value={inferenceUs} unit="μs" precision={2} />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            Metric: <code className="font-mono text-[var(--text-primary)]">luv_execution_inference_latency_microseconds</code>
          </span>
        </div>

        <div className="panel-card p-4">
          <div className="flex items-center justify-between">
            <span className="text-[11px] font-mono text-[var(--text-secondary)] uppercase">
              Telemetry Queue Depth
            </span>
            <Radio className="w-4 h-4 text-[var(--text-secondary)]" />
          </div>
          <div className="text-2xl lg:text-3xl font-mono font-bold text-[var(--text-primary)] my-2">
            <NumericValue value={queueDepth} unit="slots" />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            Capacity: 65,536 SPSC ring slots
          </span>
        </div>
      </div>

      {/* Primary Visualizations (Throughput Time Series & Risk Latency Chart) */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* Throughput Time Series */}
        <div className="panel-card">
          <div className="panel-header">
            <div className="flex items-center gap-2">
              <Activity className="w-4 h-4 text-[var(--status-ok)]" />
              <h2>Throughput Time Series (Events / Sec)</h2>
            </div>
            <StatusChip status={fresh ? 'live' : 'stale'} label={fresh ? 'Real-Time Scrape' : 'Telemetry Offline'} />
          </div>
          <div className="p-4 h-[250px] font-mono">
            {fresh && history.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <AreaChart data={history} margin={{ top: 10, right: 10, left: -15, bottom: 0 }}>
                  <defs>
                    <linearGradient id="rateGradient" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="#22c55e" stopOpacity={0.25} />
                      <stop offset="95%" stopColor="#22c55e" stopOpacity={0} />
                    </linearGradient>
                  </defs>
                  <CartesianGrid stroke="#263140" strokeDasharray="3 3" vertical={false} />
                  <XAxis dataKey="time" stroke="#8e9cad" tick={{ fontSize: 10 }} minTickGap={40} />
                  <YAxis stroke="#8e9cad" tick={{ fontSize: 10 }} />
                  <Tooltip
                    contentStyle={{ background: '#12161d', border: '1px solid #263140', fontSize: 11 }}
                    formatter={(val) => [`${Number(val).toLocaleString()} events/s`, 'Throughput']}
                  />
                  <Area
                    type="monotone"
                    dataKey="rate"
                    stroke="#22c55e"
                    strokeWidth={2}
                    fillOpacity={1}
                    fill="url(#rateGradient)"
                    isAnimationActive={false}
                  />
                </AreaChart>
              </ResponsiveContainer>
            ) : (
              <EmptyState
                title="Awaiting Telemetry Samples"
                description="Scraping Prometheus /metrics on port 9090."
                backendSource="luv_telemetry.hpp · TelemSnapshot"
              />
            )}
          </div>
          <div className="px-4 py-2 border-t border-[var(--border-subtle)] text-[10px] text-[var(--text-secondary)] font-mono flex justify-between">
            <span>Buffer: 120 rolling samples</span>
            <span>Update interval: 3s</span>
          </div>
        </div>

        {/* Risk Check Latency Chart */}
        <div className="panel-card">
          <div className="panel-header">
            <div className="flex items-center gap-2">
              <Zap className="w-4 h-4 text-[var(--accent)]" />
              <h2>Risk Check Compute Latency (Nanoseconds)</h2>
            </div>
            <span className="text-[11px] font-mono text-[var(--text-secondary)]">Single-pass hot path</span>
          </div>
          <div className="p-4 h-[250px] font-mono">
            {fresh && history.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <LineChart data={history} margin={{ top: 10, right: 10, left: -15, bottom: 0 }}>
                  <CartesianGrid stroke="#263140" strokeDasharray="3 3" vertical={false} />
                  <XAxis dataKey="time" stroke="#8e9cad" tick={{ fontSize: 10 }} minTickGap={40} />
                  <YAxis stroke="#8e9cad" tick={{ fontSize: 10 }} />
                  <Tooltip
                    contentStyle={{ background: '#12161d', border: '1px solid #263140', fontSize: 11 }}
                    formatter={(val) => [`${val} ns`, 'Risk Check']}
                  />
                  <Line
                    type="monotone"
                    dataKey="risk"
                    stroke="#3b82f6"
                    strokeWidth={2}
                    dot={false}
                    isAnimationActive={false}
                  />
                </LineChart>
              </ResponsiveContainer>
            ) : (
              <EmptyState
                title="Awaiting Risk Samples"
                description="Waiting for order risk calculations."
                backendSource="luv_execution.hpp · PreTradeRisk::check_order"
              />
            )}
          </div>
          <div className="px-4 py-2 border-t border-[var(--border-subtle)] text-[10px] text-[var(--text-secondary)] font-mono flex justify-between">
            <span>Critical path: Fixed-offset integer comparisons</span>
            <span>Zero allocations</span>
          </div>
        </div>
      </div>

      {/* Monotonic Process Counters (Section 5.3) */}
      <div className="panel-card">
        <div className="panel-header">
          <h2>Monotonic Production Counters</h2>
          <span className="text-[11px] font-mono text-[var(--text-secondary)]">
            Counters survive reset; gauges reflect last sample
          </span>
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4 divide-y md:divide-y-0 md:divide-x divide-[var(--border-subtle)] p-2">
          <div className="p-3">
            <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
              Cumulative Reconciled Fills
            </span>
            <div className="text-xl font-mono font-bold text-[var(--status-ok)] mt-1">
              <NumericValue value={fillsTotal} unit="fills" />
            </div>
            <span className="text-[10px] text-[var(--text-secondary)] block font-mono">
              luv_execution_fills_total
            </span>
          </div>

          <div className="p-3">
            <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
              Cumulative Rejections
            </span>
            <div className="text-xl font-mono font-bold text-[var(--status-warn)] mt-1">
              <NumericValue value={rejectionsTotal} unit="rejects" />
            </div>
            <span className="text-[10px] text-[var(--text-secondary)] block font-mono">
              luv_execution_rejections_total
            </span>
          </div>

          <div className="p-3">
            <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
              Dropped Fill Notifications
            </span>
            <div className="text-xl font-mono font-bold text-[var(--text-primary)] mt-1">
              <NumericValue value={wsDrops} unit="drops" />
            </div>
            <span className="text-[10px] text-[var(--text-secondary)] block font-mono">
              luv_websocket_fill_notification_drops_total
            </span>
          </div>

          <div className="p-3">
            <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
              Dropped Snapshots (SPSC)
            </span>
            <div className="text-xl font-mono font-bold text-[var(--text-primary)] mt-1">
              <NumericValue value={droppedSnapshots} unit="drops" />
            </div>
            <span className="text-[10px] text-[var(--text-secondary)] block font-mono">
              luv_telemetry_dropped_snapshots_total
            </span>
          </div>
        </div>
      </div>

      {/* Fanout & Quantile Boundary Disclosure (Section 5.3) */}
      <div className="panel-card p-4">
        <div className="flex items-start gap-3">
          <AlertTriangle className="w-5 h-5 text-amber-400 flex-shrink-0 mt-0.5" />
          <div className="space-y-1 text-xs">
            <strong className="text-[var(--text-primary)] font-medium">
              Performance Caveats & Distribution Boundary (README.md & BENCHMARKING.md)
            </strong>
            <p className="text-[var(--text-secondary)] leading-relaxed text-[11px]">
              These latency and throughput figures represent local in-memory compute on test hardware. WebSocket burst latency and N=100 fanout measurements in CI runs are report-only diagnostics and do not establish external production SLAs. Execution P50/P99 rolling quantiles are not exposed by the Prometheus endpoint; figures above are point-in-time gauges and monotonic counters.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}
