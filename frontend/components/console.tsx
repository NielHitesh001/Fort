'use client';

import React, { useEffect, useState } from 'react';
import Link from 'next/link';
import { useRouter } from 'next/navigation';
import { QueryClient, QueryClientProvider, useQuery } from '@tanstack/react-query';
import {
  Activity,
  ArrowUpRight,
  BookOpen,
  Command,
  Gauge,
  Layers,
  ListOrdered,
  Search,
  ShieldCheck,
  Terminal,
  Unplug,
  ChevronRight,
  User,
  Cpu,
} from 'lucide-react';
import { Dialog, DialogContent, DialogTitle, DialogDescription } from '@/components/ui/dialog';
import { useConsole } from '@/lib/store';
import { request, integer } from '@/lib/contracts';
import { useConnectionState } from '@/lib/connection';
import { ConnectionBadge } from './primitives/ConnectionBadge';
import { StatusChip } from './primitives/StatusChip';
import { SandboxWatermark } from './primitives/SandboxWatermark';

// Screens
import { OrderBookScreen } from './screens/OrderBookScreen';
import { ExecutionScreen } from './screens/ExecutionScreen';
import { TelemetryScreen } from './screens/TelemetryScreen';
import { SystemHealthScreen } from './screens/SystemHealthScreen';
import { AuditTrailScreen } from './screens/AuditTrailScreen';
import { PilotSummaryScreen } from './screens/PilotSummaryScreen';
import { SandboxScreen } from './screens/SandboxScreen';

const screens = [
  { id: 'book', label: 'Order Book', icon: Layers, desc: 'Real-time LOB depth and spread ladder.' },
  { id: 'execution', label: 'Execution', icon: ListOrdered, desc: 'Submit, cancel, and supervise simulator orders.' },
  { id: 'telemetry', label: 'Telemetry', icon: Activity, desc: 'Prometheus metrics and latency dashboards.' },
  { id: 'operations', label: 'System Health', icon: ShieldCheck, desc: 'Operational state, arena memory, and circuit breaker.' },
  { id: 'audit', label: 'Audit Trail', icon: BookOpen, desc: 'Read-only recovery ledger and checksum integrity.' },
  { id: 'overview', label: 'Pilot Summary', icon: Gauge, desc: 'Institutional maturity disclosure and verified metrics.' },
  { id: 'sandbox', label: 'Sandbox / API Console', icon: Terminal, desc: 'Raw REST requests, Prometheus dumps, and WebSocket inspector.' },
];

export function ConsoleApp({ screen }: { screen: string }) {
  const [client] = useState(
    () =>
      new QueryClient({
        defaultOptions: {
          queries: { retry: false, refetchOnWindowFocus: false },
          mutations: { retry: false },
        },
      })
  );

  return (
    <QueryClientProvider client={client}>
      <Workspace screen={screen} />
    </QueryClientProvider>
  );
}

function Workspace({ screen }: { screen: string }) {
  const router = useRouter();
  const { palette, setPalette, setOrderId } = useConsole();
  const [search, setSearch] = useState('');

  // Single source of truth for connection state across all screens
  const connection = useConnectionState();

  const config = useQuery({
    queryKey: ['config'],
    queryFn: async () => JSON.parse(await request('config')) as { orders: boolean; metrics: boolean; ledger: boolean },
  });

  // Normalize route alias
  let currentScreenId = screen;
  if (screen === 'order-book') currentScreenId = 'book';
  if (screen === 'system-health') currentScreenId = 'operations';
  if (screen === 'pilot-summary') currentScreenId = 'overview';
  if (screen === 'audit-trail') currentScreenId = 'audit';

  const activeScreen = screens.find((s) => s.id === currentScreenId) || screens[0];

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        setPalette(!useConsole.getState().palette);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [setPalette]);

  const navigateTo = (id: string) => {
    setPalette(false);
    setSearch('');
    router.push('/' + id);
  };

  // BUG 5 FIX: Dynamic, page-aware top-right telemetry scrape note
  let scrapeStatusNote = 'NO TELEMETRY SCRAPE YET';
  if (currentScreenId === 'sandbox') {
    scrapeStatusNote = 'DIRECT API PROBE MODE';
  } else if (!connection.isOnline) {
    scrapeStatusNote = 'ENGINE OFFLINE · NO ACTIVE SCRAPE';
  } else if (connection.age !== null) {
    scrapeStatusNote = `LAST SCRAPE: ${connection.age}s AGO`;
  }

  return (
    <div className="shell-container">
      {/* LEFT NAVIGATION (Section 5 Architecture + Part 2 Sandbox nav) */}
      <aside className="sidebar-nav">
        <Link href="/overview" className="h-[72px] flex items-center gap-3 px-5 border-b border-[var(--border-subtle)] bg-[var(--bg-canvas)]">
          <div className="w-8 h-8 rounded bg-[var(--accent)] flex items-center justify-center font-mono font-bold text-white text-sm shadow-md">
            C
          </div>
          <div>
            <span className="font-bold text-sm tracking-wider block text-[var(--text-primary)]">
              CORRIDOR
            </span>
            <span className="text-[10px] font-mono text-[var(--text-secondary)] block">
              INSTITUTIONAL FX
            </span>
          </div>
        </Link>

        <div className="px-5 py-3 text-[10px] font-mono text-[var(--text-secondary)] tracking-widest border-b border-[var(--border-subtle)]/50">
          EXECUTION TERMINAL
        </div>

        <nav className="p-3 flex-1 space-y-1" aria-label="Main Navigation">
          {screens.map((item, idx) => {
            const isActive = item.id === currentScreenId;
            return (
              <Link
                key={item.id}
                href={'/' + item.id}
                className={`nav-item ${isActive ? 'active' : ''}`}
                aria-current={isActive ? 'page' : undefined}
              >
                <item.icon className={`w-4 h-4 ${isActive ? 'text-[var(--accent)]' : 'text-[var(--text-secondary)]'}`} />
                <span className="flex-1">{item.label}</span>
                <span className="font-mono text-[10px] text-[var(--text-secondary)] opacity-60">
                  0{idx + 1}
                </span>
              </Link>
            );
          })}
        </nav>

        <div className="p-4 border-t border-[var(--border-subtle)] mt-auto space-y-3 bg-[var(--bg-canvas)]">
          <button
            className="w-full flex items-center justify-between p-2 rounded bg-[var(--bg-surface)] border border-[var(--border-subtle)] text-xs text-[var(--text-secondary)] hover:border-[var(--accent)] transition-state"
            onClick={() => setPalette(true)}
          >
            <span className="flex items-center gap-2">
              <Search className="w-3.5 h-3.5" /> Jump to screen…
            </span>
            <kbd className="font-mono text-[10px] px-1 py-0.5 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)]">
              ⌘K
            </kbd>
          </button>

          <div className="flex items-center justify-between text-[10px] font-mono text-[var(--text-secondary)]">
            <span>FORT ENGINE · C++20</span>
            <StatusChip status={connection.isOnline ? 'live' : 'offline'} label={connection.isOnline ? 'PARKED' : 'OFFLINE'} />
          </div>
        </div>
      </aside>

      {/* MAIN CONTENT REGION */}
      <div className="main-content">
        {/* TOP BAR */}
        <header className="h-14 border-b border-[var(--border-subtle)] bg-[var(--bg-surface)] px-6 flex items-center justify-between select-none sticky top-0 z-20">
          <div className="flex items-center gap-2 text-xs text-[var(--text-secondary)]">
            <span>Workspace</span>
            <ChevronRight className="w-3.5 h-3.5" />
            <span className="text-[var(--text-primary)] font-medium">{activeScreen.label}</span>
          </div>

          <div className="flex items-center gap-4">
            {/* Global Connection Badge derived from unified connection store */}
            <ConnectionBadge
              wsConnected={connection.wsConnected}
              healthOk={connection.healthOk}
              feedFresh={connection.feedFresh}
              ageSeconds={connection.age}
            />

            {/* Environment Badge */}
            <span
              data-testid="env-badge"
              className="px-2 py-0.5 rounded bg-amber-950/50 border border-amber-500/40 text-amber-300 font-mono text-[10px] font-semibold tracking-wider uppercase"
            >
              SIMULATION (RESEARCH)
            </span>

            {/* Account / Session Control */}
            <div className="flex items-center gap-2 border-l border-[var(--border-subtle)] pl-4 text-xs font-mono text-[var(--text-secondary)]">
              <div className="w-6 h-6 rounded-full bg-[var(--bg-surface-2)] border border-[var(--border-subtle)] flex items-center justify-center">
                <User className="w-3.5 h-3.5 text-[var(--accent)]" />
              </div>
              <span className="hidden sm:inline">PILOT-DESK-01</span>
            </div>
          </div>
        </header>

        {/* PERSISTENT SANDBOX WATERMARK BANNER (Section 8 Hard Rule) */}
        <SandboxWatermark mode="SIMULATION" />

        {/* SCREEN VIEWPORT */}
        <main className="p-6 max-w-[1700px] w-full mx-auto flex-1">
          {/* Screen Title & Header */}
          <div className="flex flex-wrap items-center justify-between gap-4 mb-6">
            <div>
              <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider block">
                CORRIDOR CONSOLE / {activeScreen.label.toUpperCase()}
              </span>
              <h1 className="text-xl font-bold text-[var(--text-primary)] mt-0.5">
                {activeScreen.label}
              </h1>
              <p className="text-xs text-[var(--text-secondary)] mt-0.5">
                {activeScreen.desc}
              </p>
            </div>

            <div className="text-right">
              <span className="text-[10px] font-mono text-[var(--text-secondary)] block">
                {scrapeStatusNote}
              </span>
              <span className="text-[10px] text-[var(--text-secondary)] font-mono">
                Port 9090 (/metrics) · Port 8080 (/api/v1)
              </span>
            </div>
          </div>

          {/* Render Active Screen */}
          {currentScreenId === 'book' && (
            <OrderBookScreen
              feedFresh={connection.feedFresh}
              wsConnected={connection.wsConnected}
              healthOk={connection.healthOk}
              isOnline={connection.isOnline}
              tickRate={connection.tickRate}
              lastTickTime={connection.lastTickTime}
            />
          )}

          {currentScreenId === 'execution' && (
            <ExecutionScreen
              enabled={!config.isError && connection.healthOk}
              halted={connection.halted}
            />
          )}

          {currentScreenId === 'telemetry' && (
            <TelemetryScreen
              metrics={connection.metrics}
              history={connection.history}
              fresh={connection.isOnline}
              age={connection.age}
            />
          )}

          {currentScreenId === 'operations' && (
            <SystemHealthScreen
              healthOk={connection.healthOk}
              wsConnected={connection.wsConnected}
              feedFresh={connection.feedFresh}
              feedState={connection.feedState}
              isOnline={connection.isOnline}
              halted={connection.halted}
              metrics={connection.metrics}
            />
          )}

          {currentScreenId === 'audit' && (
            <AuditTrailScreen />
          )}

          {currentScreenId === 'overview' && (
            <PilotSummaryScreen
              metrics={connection.metrics}
              fresh={connection.isOnline}
              healthOk={connection.healthOk}
            />
          )}

          {currentScreenId === 'sandbox' && (
            <SandboxScreen config={config.data} />
          )}
        </main>
      </div>

      {/* COMMAND PALETTE MODAL (Cmd+K) */}
      <Dialog open={palette} onOpenChange={setPalette}>
        <DialogContent className="max-w-md">
          <DialogTitle>Command Palette</DialogTitle>
          <DialogDescription>
            Navigate directly to any trading or operations subsystem.
          </DialogDescription>
          <input
            autoFocus
            aria-label="Search screens or enter order ID"
            placeholder="Search screens or enter order ID…"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            className="my-3 font-mono text-xs"
          />
          <div className="space-y-1">
            {screens
              .filter((s) => s.label.toLowerCase().includes(search.toLowerCase()))
              .map((s) => (
                <button
                  key={s.id}
                  onClick={() => navigateTo(s.id)}
                  className="w-full flex items-center justify-between p-2.5 rounded hover:bg-[var(--bg-surface-2)] text-left text-xs font-medium text-[var(--text-primary)]"
                >
                  <div className="flex items-center gap-2.5">
                    <s.icon className="w-4 h-4 text-[var(--accent)]" />
                    <span>{s.label}</span>
                  </div>
                  <ArrowUpRight className="w-3.5 h-3.5 text-[var(--text-secondary)]" />
                </button>
              ))}

            {/^[1-9]\d{0,19}$/.test(search) && (
              <button
                onClick={() => {
                  setOrderId(integer(search, 1n));
                  navigateTo('execution');
                }}
                className="w-full flex items-center justify-between p-2.5 rounded bg-[var(--bg-surface-2)] text-left text-xs font-medium text-[var(--accent)]"
              >
                <div className="flex items-center gap-2">
                  <Search className="w-4 h-4" />
                  <span>Inspect Order #{search}</span>
                </div>
                <ArrowUpRight className="w-3.5 h-3.5" />
              </button>
            )}
          </div>
        </DialogContent>
      </Dialog>
    </div>
  );
}
