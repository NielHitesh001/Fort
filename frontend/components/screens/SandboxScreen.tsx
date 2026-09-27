'use client';

import React, { useState, useEffect, useRef } from 'react';
import {
  Terminal,
  Send,
  RefreshCw,
  Radio,
  Sliders,
  Play,
  Pause,
  Trash2,
  CheckCircle2,
  XCircle,
  AlertTriangle,
  Code,
  Server,
  Activity,
  Layers,
  FileText,
} from 'lucide-react';
import { Button } from '@/components/ui/button';
import { StatusChip } from '@/components/primitives/StatusChip';
import { useConnectionState } from '@/lib/connection';
import { request } from '@/lib/contracts';

interface WsFrame {
  id: string;
  time: string;
  type: 'in' | 'out' | 'status';
  data: string;
}

export function SandboxScreen({
  config = { orders: false, metrics: false, ledger: false },
}: {
  config?: { orders: boolean; metrics: boolean; ledger: boolean };
}) {
  const connection = useConnectionState();

  // 1. Raw REST Request State
  const [method, setMethod] = useState<'GET' | 'POST' | 'DELETE'>('GET');
  const [endpointPath, setEndpointPath] = useState('/api/v1/positions');
  const [requestHeaders, setRequestHeaders] = useState('{\n  "Accept": "application/json"\n}');
  const [requestBody, setRequestBody] = useState('{\n  "symbol_idx": 267,\n  "side": "buy",\n  "qty": 100,\n  "price": 1502500\n}');
  const [restLoading, setRestLoading] = useState(false);
  const [restResponse, setRestResponse] = useState<{
    status: number | null;
    statusText: string;
    headers: Record<string, string>;
    body: string;
    durationMs: number | null;
  } | null>(null);

  // 2. Raw Metrics Viewer State
  const [metricsLoading, setMetricsLoading] = useState(false);
  const [rawMetrics, setRawMetrics] = useState<string>('');
  const [metricsFetchedAt, setMetricsFetchedAt] = useState<string | null>(null);
  const [metricsError, setMetricsError] = useState<string | null>(null);

  // 3. WebSocket Inspector State
  const [wsStreamOrderId, setWsStreamOrderId] = useState('1001');
  const [wsActive, setWsActive] = useState(false);
  const [wsPaused, setWsPaused] = useState(false);
  const [wsFrames, setWsFrames] = useState<WsFrame[]>([]);
  const wsRef = useRef<WebSocket | null>(null);

  // Handle REST dispatch
  async function handleSendRest() {
    setRestLoading(true);
    setRestResponse(null);
    const start = performance.now();

    // Route conversion to corridor proxy bridge
    let proxyPath = '';
    const cleanPath = endpointPath.replace(/^\/+/, '');
    if (cleanPath === 'api/v1/positions' || cleanPath === 'positions') proxyPath = 'positions';
    else if (cleanPath === 'api/v1/orders' || cleanPath === 'orders') proxyPath = 'orders';
    else if (cleanPath.startsWith('api/v1/orders/') || cleanPath.startsWith('orders/')) {
      proxyPath = cleanPath.replace(/^api\/v1\//, '');
    } else if (cleanPath === 'metrics') proxyPath = 'metrics';
    else if (cleanPath === 'healthz' || cleanPath === 'health') proxyPath = 'health';
    else proxyPath = cleanPath;

    try {
      const parsedHeaders = requestHeaders.trim() ? JSON.parse(requestHeaders) : {};
      const options: RequestInit = {
        method,
        headers: {
          'Content-Type': 'application/json',
          ...parsedHeaders,
        },
      };

      if (method === 'POST') {
        options.body = requestBody;
      }

      const res = await fetch(`/api/corridor/${proxyPath}`, {
        ...options,
        cache: 'no-store',
      });

      const elapsed = Math.round(performance.now() - start);
      const text = await res.text();

      const respHeaders: Record<string, string> = {};
      res.headers.forEach((val, key) => {
        respHeaders[key] = val;
      });

      let formattedBody = text;
      try {
        formattedBody = JSON.stringify(JSON.parse(text), null, 2);
      } catch {
        // Plain text
      }

      setRestResponse({
        status: res.status,
        statusText: res.statusText || (res.ok ? 'OK' : 'Error'),
        headers: respHeaders,
        body: formattedBody,
        durationMs: elapsed,
      });
    } catch (err) {
      const elapsed = Math.round(performance.now() - start);
      setRestResponse({
        status: 0,
        statusText: 'Network / Connection Failure',
        headers: {},
        body: JSON.stringify({ error: (err as Error).message, note: 'Engine bridge or target port unreachable' }, null, 2),
        durationMs: elapsed,
      });
    } finally {
      setRestLoading(false);
    }
  }

  // Handle Raw Metrics fetch
  async function handleFetchMetrics() {
    setMetricsLoading(true);
    setMetricsError(null);
    try {
      const text = await request('metrics');
      setRawMetrics(text);
      setMetricsFetchedAt(new Date().toLocaleTimeString('en-US', { hour12: false }));
    } catch (err) {
      setMetricsError((err as Error).message);
      setRawMetrics('');
    } finally {
      setMetricsLoading(false);
    }
  }

  // WebSocket connect/disconnect
  function toggleWebSocket() {
    if (wsActive) {
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
      setWsActive(false);
      addWsFrame('status', 'WebSocket connection closed by user');
      return;
    }

    try {
      const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const wsUrl = `${proto}//${window.location.host}/api/corridor/stream/${wsStreamOrderId}`;
      addWsFrame('status', `Connecting to RFC 6455 stream: ${wsUrl}…`);

      const socket = new WebSocket(wsUrl);
      wsRef.current = socket;

      socket.onopen = () => {
        setWsActive(true);
        addWsFrame('status', `Connected to execution stream #${wsStreamOrderId} (RFC 6455 established)`);
      };

      socket.onmessage = (e) => {
        if (!wsPaused) {
          addWsFrame('in', e.data);
        }
      };

      socket.onerror = () => {
        addWsFrame('status', 'WebSocket error encountered (Upstream engine may be offline)');
      };

      socket.onclose = (e) => {
        setWsActive(false);
        wsRef.current = null;
        addWsFrame('status', `WebSocket closed (Code: ${e.code}, Reason: ${e.reason || 'None'})`);
      };
    } catch (err) {
      addWsFrame('status', `Failed to initialize WebSocket: ${(err as Error).message}`);
    }
  }

  function addWsFrame(type: 'in' | 'out' | 'status', data: string) {
    setWsFrames((prev) => [
      {
        id: Math.random().toString(36).substring(2, 9),
        time: new Date().toLocaleTimeString('en-US', { hour12: false }),
        type,
        data,
      },
      ...prev.slice(0, 99),
    ]);
  }

  useEffect(() => {
    return () => {
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, []);

  return (
    <div className="space-y-6">
      {/* INTERNAL TOOL BANNER */}
      <div
        data-testid="sandbox-banner"
        className="p-3 bg-blue-950/40 border border-blue-500/40 text-blue-300 rounded flex items-center justify-between text-xs font-mono"
      >
        <div className="flex items-center gap-2">
          <Terminal className="w-4 h-4 text-blue-400" />
          <span className="font-bold tracking-wider uppercase text-blue-200">
            INTERNAL TOOL — NOT PART OF PILOT DEMO FLOW
          </span>
        </div>
        <span className="text-[10px] text-blue-400">
          Direct REST, WebSocket, & Prometheus Probe Harness
        </span>
      </div>

      {/* CONNECTION STATE DEBUG & ENVIRONMENT READOUT (Panels 4 & 5) */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* Panel 4: Shared Connection Store Inspector */}
        <div className="panel-card">
          <div className="panel-header">
            <div className="flex items-center gap-2">
              <Activity className="w-4 h-4 text-[var(--accent)]" />
              <h2>Connection Store Inspector (useConnectionState)</h2>
            </div>
            <StatusChip
              status={connection.isOnline ? 'healthy' : 'unavailable'}
              label={connection.isOnline ? 'Store: Online' : 'Store: Offline'}
            />
          </div>
          <div className="p-4 grid grid-cols-2 sm:grid-cols-3 gap-3 font-mono text-xs">
            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">HEALTH OK</span>
              <span className={connection.healthOk ? 'text-[var(--status-ok)]' : 'text-[var(--status-critical)]'}>
                {connection.healthStatusText}
              </span>
            </div>

            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">FEED STATE</span>
              <span className="font-semibold uppercase text-[var(--text-primary)]">
                {connection.feedState}
              </span>
            </div>

            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">TICK RATE</span>
              <span className="font-semibold text-[var(--text-primary)]">
                {connection.tickRate} hz
              </span>
            </div>

            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">LAST TICK TIME</span>
              <span className="text-[var(--text-secondary)]">
                {connection.lastTickTime || 'None'}
              </span>
            </div>

            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">SCRAPE AGE</span>
              <span className="text-[var(--text-secondary)]">
                {connection.age !== null ? `${connection.age}s ago` : 'None'}
              </span>
            </div>

            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">RISK HALTED</span>
              <span className={connection.halted ? 'text-red-400 font-bold' : 'text-emerald-400'}>
                {connection.halted ? 'HALTED' : 'NORMAL'}
              </span>
            </div>
          </div>
        </div>

        {/* Panel 5: Environment & Config Readout */}
        <div className="panel-card">
          <div className="panel-header">
            <div className="flex items-center gap-2">
              <Server className="w-4 h-4 text-purple-400" />
              <h2>Environment & Subsystem Topology</h2>
            </div>
            <span className="text-[11px] font-mono text-[var(--text-secondary)]">Loopback Bound</span>
          </div>
          <div className="p-4 grid grid-cols-2 gap-3 font-mono text-xs">
            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">EXECUTION MODE</span>
              <span className="font-semibold text-amber-300">SIMULATION (RESEARCH)</span>
            </div>

            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">FRONTEND PORT</span>
              <span>127.0.0.1:3000</span>
            </div>

            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">ENGINE REST (PORT 8080)</span>
              <span className={config.orders ? 'text-[var(--status-ok)]' : 'text-[var(--text-secondary)]'}>
                {config.orders ? 'Token Configured' : 'No Token Configured'}
              </span>
            </div>

            <div className="p-2 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              <span className="text-[10px] text-[var(--text-secondary)] block">PROMETHEUS (PORT 9090)</span>
              <span className={config.metrics ? 'text-[var(--status-ok)]' : 'text-[var(--text-secondary)]'}>
                {config.metrics ? 'Token Configured' : 'No Token Configured'}
              </span>
            </div>
          </div>
        </div>
      </div>

      {/* Panel 1: Raw REST Request Panel */}
      <div className="panel-card">
        <div className="panel-header">
          <div className="flex items-center gap-2">
            <Terminal className="w-4 h-4 text-[var(--accent)]" />
            <h2>Raw REST Control Plane Client</h2>
          </div>
          <span className="text-[11px] font-mono text-[var(--text-secondary)]">
            Transparent Bridge to :8080 / :9090
          </span>
        </div>

        <div className="p-4 space-y-4 font-mono text-xs">
          {/* Method, Path, and Submit */}
          <div className="flex flex-wrap items-center gap-2">
            <select
              aria-label="HTTP Method"
              className="w-28 font-mono text-xs bg-[var(--bg-canvas)] border border-[var(--border-subtle)] rounded px-2.5 py-1.5 text-[var(--text-primary)] font-bold"
              value={method}
              onChange={(e) => setMethod(e.target.value as 'GET' | 'POST' | 'DELETE')}
            >
              <option value="GET">GET</option>
              <option value="POST">POST</option>
              <option value="DELETE">DELETE</option>
            </select>

            <input
              aria-label="Request Path"
              type="text"
              className="flex-1 min-w-[240px] font-mono text-xs px-3 py-1.5"
              placeholder="/api/v1/positions"
              value={endpointPath}
              onChange={(e) => setEndpointPath(e.target.value)}
            />

            <Button
              className="gap-1.5 h-8 text-xs bg-[var(--accent)] hover:bg-blue-600 text-white font-medium"
              onClick={handleSendRest}
              disabled={restLoading}
            >
              <Send className="w-3.5 h-3.5" />
              {restLoading ? 'Sending…' : 'Send Request'}
            </Button>
          </div>

          {/* Headers & Body editors for POST */}
          <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
            <div>
              <label className="text-[10px] text-[var(--text-secondary)] uppercase block mb-1">
                Request Headers (JSON)
              </label>
              <textarea
                rows={3}
                className="w-full font-mono text-[11px] p-2 bg-[var(--bg-canvas)] border border-[var(--border-subtle)] rounded text-[var(--text-primary)]"
                value={requestHeaders}
                onChange={(e) => setRequestHeaders(e.target.value)}
              />
            </div>

            <div>
              <label className="text-[10px] text-[var(--text-secondary)] uppercase block mb-1">
                Request Body (JSON) {method === 'GET' && '(Ignored on GET)'}
              </label>
              <textarea
                rows={3}
                className="w-full font-mono text-[11px] p-2 bg-[var(--bg-canvas)] border border-[var(--border-subtle)] rounded text-[var(--text-primary)]"
                value={requestBody}
                onChange={(e) => setRequestBody(e.target.value)}
                disabled={method === 'GET'}
              />
            </div>
          </div>

          {/* Response Output */}
          {restResponse && (
            <div className="mt-4 border border-[var(--border-subtle)] rounded bg-[var(--bg-canvas)] p-3 space-y-2">
              <div className="flex items-center justify-between text-xs pb-2 border-b border-[var(--border-subtle)]">
                <div className="flex items-center gap-2">
                  <span
                    className={`font-bold px-2 py-0.5 rounded text-[11px] ${
                      restResponse.status && restResponse.status >= 200 && restResponse.status < 300
                        ? 'bg-emerald-950 text-emerald-400 border border-emerald-500/30'
                        : 'bg-red-950 text-red-400 border border-red-500/30'
                    }`}
                  >
                    STATUS: {restResponse.status || 'FAILED'} {restResponse.statusText}
                  </span>
                  <span className="text-[var(--text-secondary)] text-[11px]">
                    Latency: {restResponse.durationMs}ms
                  </span>
                </div>
              </div>

              <div>
                <span className="text-[10px] text-[var(--text-secondary)] uppercase block mb-1">
                  Response Body
                </span>
                <pre className="p-3 bg-[var(--bg-surface)] rounded text-[11px] font-mono text-[var(--text-primary)] overflow-x-auto max-h-60">
                  {restResponse.body}
                </pre>
              </div>
            </div>
          )}
        </div>
      </div>

      {/* Panel 2 & Panel 3: Raw Metrics Viewer & WebSocket Inspector */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* Panel 2: Raw Metrics Viewer */}
        <div className="panel-card">
          <div className="panel-header">
            <div className="flex items-center gap-2">
              <Code className="w-4 h-4 text-emerald-400" />
              <h2>Verbatim Prometheus Exporter (:9090/metrics)</h2>
            </div>
            <Button
              size="sm"
              variant="outline"
              className="h-7 text-xs gap-1.5 font-mono"
              onClick={handleFetchMetrics}
              disabled={metricsLoading}
            >
              <RefreshCw className={`w-3 h-3 ${metricsLoading ? 'animate-spin' : ''}`} />
              Fetch /metrics
            </Button>
          </div>

          <div className="p-4 space-y-2">
            <div className="flex justify-between items-center text-[10px] font-mono text-[var(--text-secondary)]">
              <span>Ground truth raw payload</span>
              <span>{metricsFetchedAt ? `Fetched: ${metricsFetchedAt}` : 'Not fetched yet'}</span>
            </div>

            {metricsError ? (
              <div className="p-3 rounded bg-red-950/40 border border-red-500/40 text-red-300 font-mono text-xs">
                Failed to fetch /metrics: {metricsError}
              </div>
            ) : rawMetrics ? (
              <pre className="p-3 bg-[var(--bg-canvas)] border border-[var(--border-subtle)] rounded text-[10px] font-mono text-emerald-300 overflow-x-auto max-h-72 leading-relaxed">
                {rawMetrics}
              </pre>
            ) : (
              <div className="p-8 text-center border border-dashed border-[var(--border-subtle)] rounded text-xs text-[var(--text-secondary)] font-mono">
                Click &quot;Fetch /metrics&quot; above to inspect verbatim Prometheus text output.
              </div>
            )}
          </div>
        </div>

        {/* Panel 3: WebSocket Inspector */}
        <div className="panel-card">
          <div className="panel-header">
            <div className="flex items-center gap-2">
              <Radio className="w-4 h-4 text-blue-400" />
              <h2>RFC 6455 WebSocket Live Inspector</h2>
            </div>
            <div className="flex items-center gap-1.5">
              <Button
                size="sm"
                variant="outline"
                className="h-7 text-xs gap-1"
                onClick={() => setWsPaused(!wsPaused)}
                disabled={!wsActive}
              >
                {wsPaused ? <Play className="w-3 h-3" /> : <Pause className="w-3 h-3" />}
                {wsPaused ? 'Resume' : 'Pause'}
              </Button>
              <Button
                size="sm"
                variant="ghost"
                className="h-7 text-xs"
                onClick={() => setWsFrames([])}
              >
                <Trash2 className="w-3 h-3" />
              </Button>
            </div>
          </div>

          <div className="p-4 space-y-3 font-mono text-xs">
            <div className="flex items-center gap-2">
              <input
                aria-label="Order ID to Stream"
                type="text"
                placeholder="Order ID (e.g. 1001)"
                className="font-mono text-xs flex-1 h-8"
                value={wsStreamOrderId}
                onChange={(e) => setWsStreamOrderId(e.target.value)}
                disabled={wsActive}
              />
              <Button
                size="sm"
                variant={wsActive ? 'outline' : 'default'}
                className={`h-8 text-xs font-mono ${
                  wsActive ? 'border-red-500 text-red-400 hover:bg-red-950' : 'bg-blue-600 text-white'
                }`}
                onClick={toggleWebSocket}
              >
                {wsActive ? 'Disconnect' : 'Connect Stream'}
              </Button>
            </div>

            <div className="border border-[var(--border-subtle)] rounded bg-[var(--bg-canvas)] p-2 h-64 overflow-y-auto space-y-1.5">
              {wsFrames.length === 0 ? (
                <div className="h-full flex items-center justify-center text-[11px] text-[var(--text-secondary)]">
                  No frames received. Enter an order ID and click &quot;Connect Stream&quot;.
                </div>
              ) : (
                wsFrames.map((f) => (
                  <div
                    key={f.id}
                    className={`p-1.5 rounded text-[11px] border font-mono ${
                      f.type === 'status'
                        ? 'bg-blue-950/20 border-blue-500/20 text-blue-300'
                        : f.type === 'in'
                        ? 'bg-[var(--bg-surface)] border-[var(--border-subtle)] text-[var(--status-ok)]'
                        : 'bg-[var(--bg-surface)] border-[var(--border-subtle)] text-purple-300'
                    }`}
                  >
                    <div className="flex items-center justify-between text-[9px] text-[var(--text-secondary)] mb-0.5">
                      <span>[{f.time}] {f.type.toUpperCase()}</span>
                    </div>
                    <pre className="whitespace-pre-wrap">{f.data}</pre>
                  </div>
                ))
              )}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
