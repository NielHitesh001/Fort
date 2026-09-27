'use client';

import React, { useState, useEffect } from 'react';
import { Layers, AlertTriangle, RefreshCw, BarChart3, Database } from 'lucide-react';
import { StatusChip } from '@/components/primitives/StatusChip';
import { NumericValue } from '@/components/primitives/NumericValue';
import { ConnectionBadge } from '@/components/primitives/ConnectionBadge';
import { EmptyState } from '@/components/primitives/EmptyState';
import { Button } from '@/components/ui/button';
import { fixed } from '@/lib/contracts';

interface PriceLevelRow {
  price: string;
  qty: number;
  orders: number;
  depth: number;
}

const DEFAULT_SYMBOLS = [
  { index: 267, ticker: 'AAPL', name: 'Apple Inc.' },
  { index: 0, ticker: 'EUR/USD', name: 'Euro / US Dollar Spot' },
  { index: 1, ticker: 'USD/INR', name: 'US Dollar / Indian Rupee Spot' },
  { index: 2, ticker: 'GBP/USD', name: 'British Pound / US Dollar' },
  { index: 3, ticker: 'USD/JPY', name: 'US Dollar / Japanese Yen' },
];

export function OrderBookScreen({
  feedFresh = true,
  wsConnected = false,
  healthOk = true,
  tickRate = 0,
}: {
  feedFresh?: boolean;
  wsConnected?: boolean;
  healthOk?: boolean;
  tickRate?: number;
}) {
  const [selectedSymbolIdx, setSelectedSymbolIdx] = useState<number>(267);
  const [simulatedFeedStalled, setSimulatedFeedStalled] = useState(false);
  const [lastUpdate, setLastUpdate] = useState<string>('');

  // Sample order book data initialized from in-process LOB state structure
  const [bids, setBids] = useState<PriceLevelRow[]>([
    { price: '1502500', qty: 420, orders: 4, depth: 420 },
    { price: '1502400', qty: 650, orders: 7, depth: 1070 },
    { price: '1502300', qty: 890, orders: 9, depth: 1960 },
    { price: '1502200', qty: 1200, orders: 12, depth: 3160 },
    { price: '1502100', qty: 780, orders: 6, depth: 3940 },
    { price: '1502000', qty: 1540, orders: 16, depth: 5480 },
    { price: '1501900', qty: 920, orders: 8, depth: 6400 },
    { price: '1501800', qty: 1100, orders: 11, depth: 7500 },
  ]);

  const [asks, setAsks] = useState<PriceLevelRow[]>([
    { price: '1502600', qty: 380, orders: 3, depth: 380 },
    { price: '1502700', qty: 590, orders: 6, depth: 970 },
    { price: '1502800', qty: 940, orders: 8, depth: 1910 },
    { price: '1502900', qty: 1150, orders: 14, depth: 3060 },
    { price: '1503000', qty: 820, orders: 7, depth: 3880 },
    { price: '1503100', qty: 1600, orders: 16, depth: 5480 },
    { price: '1503200', qty: 950, orders: 9, depth: 6430 },
    { price: '1503300', qty: 1250, orders: 13, depth: 7680 },
  ]);

  const maxVisibleDepth = Math.max(
    bids[bids.length - 1]?.depth || 1,
    asks[asks.length - 1]?.depth || 1
  );

  const bestBid = bids[0]?.price ? BigInt(bids[0].price) : 0n;
  const bestAsk = asks[0]?.price ? BigInt(asks[0].price) : 0n;
  const spreadUnits = bestAsk > bestBid ? Number(bestAsk - bestBid) : 0;
  const spreadFixed = (spreadUnits / 10000).toFixed(4);
  const midPrice = bestBid && bestAsk ? Number(bestBid + bestAsk) / 20000 : 0;
  const spreadBps = midPrice > 0 ? ((spreadUnits / 10000 / midPrice) * 10000).toFixed(2) : '0.00';

  useEffect(() => {
    setLastUpdate(new Date().toLocaleTimeString('en-US', { hour12: false }));
  }, []);

  const isFeedStalled = !feedFresh || simulatedFeedStalled;
  const selectedSymbol = DEFAULT_SYMBOLS.find((s) => s.index === selectedSymbolIdx);

  return (
    <div className="space-y-4">
      {/* Header Strip */}
      <div className="panel-card p-4">
        <div className="flex flex-wrap items-center justify-between gap-4">
          <div className="flex items-center gap-4">
            <div>
              <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider block">
                Selected Instrument
              </span>
              <div className="flex items-center gap-2 mt-1">
                <select
                  aria-label="Select trading instrument"
                  className="w-48 font-mono text-sm bg-[var(--bg-canvas)] border border-[var(--border-subtle)] rounded px-2.5 py-1 text-[var(--text-primary)] font-semibold"
                  value={selectedSymbolIdx}
                  onChange={(e) => setSelectedSymbolIdx(Number(e.target.value))}
                >
                  {DEFAULT_SYMBOLS.map((s) => (
                    <option key={s.index} value={s.index}>
                      {s.ticker} (Symbol #{s.index})
                    </option>
                  ))}
                </select>
                <StatusChip status="live" label="LOB Tracked" />
              </div>
            </div>

            <div className="border-l border-[var(--border-subtle)] pl-4 hidden sm:block">
              <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider block">
                Instrument Details
              </span>
              <span className="text-xs text-[var(--text-primary)] mt-1 block">
                {selectedSymbol?.name || `Symbol Index ${selectedSymbolIdx}`} · Bounded 16 orders/level
              </span>
            </div>
          </div>

          <div className="flex items-center gap-3">
            <ConnectionBadge
              wsConnected={wsConnected}
              healthOk={healthOk}
              feedFresh={!isFeedStalled}
            />
            <div className="text-right">
              <span className="text-[10px] font-mono text-[var(--text-secondary)] block">
                LAST TICK UPDATE
              </span>
              <span className="text-xs font-mono text-[var(--text-primary)]">
                {lastUpdate || '—'}
              </span>
            </div>
          </div>
        </div>
      </div>

      {/* FEED STALLED CRITICAL BANNER (Section 5.1) */}
      {isFeedStalled && (
        <div
          data-testid="feed-stalled-banner"
          className="p-3.5 bg-red-950/80 border border-[var(--status-critical)] text-[var(--status-critical)] rounded flex items-center justify-between gap-3 text-xs"
          role="alert"
        >
          <div className="flex items-center gap-2.5">
            <AlertTriangle className="w-5 h-5 flex-shrink-0 animate-pulse text-[var(--status-critical)]" />
            <div>
              <strong className="font-mono uppercase font-bold text-red-200 tracking-wide">
                FEED STALLED — Data frozen as of {lastUpdate}
              </strong>
              <p className="text-red-300 text-[11px] mt-0.5">
                Feed message age exceeds 100ms threshold (luv_feed / luv_telemetry). Stale book is frozen; new order entries must exercise caution.
              </p>
            </div>
          </div>
          <Button
            size="sm"
            variant="outline"
            className="border-red-600 text-red-200 hover:bg-red-900/50"
            onClick={() => setSimulatedFeedStalled(false)}
          >
            Acknowledge
          </Button>
        </div>
      )}

      {/* Spread & Top of Book Bar */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase">
            Best Bid
          </span>
          <div className="text-lg font-mono font-bold text-[var(--status-ok)] mt-1">
            {fixed(bids[0]?.price || 0)}
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] font-mono">
            Size: {bids[0]?.qty.toLocaleString()} units
          </span>
        </div>

        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase">
            Best Ask
          </span>
          <div className="text-lg font-mono font-bold text-[var(--status-critical)] mt-1">
            {fixed(asks[0]?.price || 0)}
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] font-mono">
            Size: {asks[0]?.qty.toLocaleString()} units
          </span>
        </div>

        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase">
            Spread (Tick / Bps)
          </span>
          <div className="text-lg font-mono font-bold text-[var(--accent)] mt-1">
            {spreadFixed} <span className="text-xs font-normal text-[var(--text-secondary)]">({spreadBps} bps)</span>
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] font-mono">
            Diff: {spreadUnits} raw ticks
          </span>
        </div>

        <div className="panel-card p-3">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase">
            Tick Engine Rate
          </span>
          <div className="text-lg font-mono font-bold text-[var(--text-primary)] mt-1">
            <NumericValue value={tickRate} unit="events/s" />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] font-mono">
            Source: luv_execution_tick_rate_hz
          </span>
        </div>
      </div>

      {/* Two-Column Limit Order Book Ladder (Section 5.1) */}
      <div className="panel-card">
        <div className="panel-header">
          <div className="flex items-center gap-2">
            <Layers className="w-4 h-4 text-[var(--accent)]" />
            <h2>Live Limit Order Book (LOB Depth)</h2>
          </div>
          <span className="text-[11px] font-mono text-[var(--text-secondary)]">
            Bids: Descending · Asks: Ascending · Capacity: 16 orders/lvl
          </span>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 divide-y md:divide-y-0 md:divide-x divide-[var(--border-subtle)]">
          {/* BIDS COLUMN (Left, Descending: Best bid at top) */}
          <div>
            <div className="bg-[var(--bg-surface-2)] px-4 py-2 flex items-center justify-between text-[11px] font-mono font-semibold text-[var(--status-ok)] border-b border-[var(--border-subtle)]">
              <span>BIDS (BUY)</span>
              <span className="text-[var(--text-secondary)] font-normal text-[10px]">
                Cumulative Vol / Price
              </span>
            </div>
            <table className="dense-table" aria-label="Bids Ladder">
              <thead>
                <tr>
                  <th className="w-16">Orders</th>
                  <th className="text-right">Size</th>
                  <th className="text-right">Cumul Depth</th>
                  <th className="text-right text-[var(--status-ok)]">Bid Price</th>
                </tr>
              </thead>
              <tbody>
                {bids.map((row, idx) => {
                  const depthPct = Math.min(100, Math.round((row.depth / maxVisibleDepth) * 100));
                  return (
                    <tr key={row.price} className="ladder-cell transition-state hover:bg-[var(--bg-surface-2)]">
                      <td className="text-[11px] text-[var(--text-secondary)] font-mono">
                        {row.orders} <span className="text-[9px] text-[var(--text-secondary)]/50">/16</span>
                      </td>
                      <td className="text-right font-mono font-medium">{row.qty.toLocaleString()}</td>
                      <td className="text-right font-mono text-[var(--text-secondary)] text-[11px]">
                        {row.depth.toLocaleString()}
                      </td>
                      <td className="text-right font-mono font-bold text-[var(--status-ok)] relative">
                        <div className="depth-bar-bid" style={{ width: `${depthPct}%` }} />
                        <span className="relative z-10">{fixed(row.price)}</span>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>

          {/* ASKS COLUMN (Right, Ascending: Best ask at top) */}
          <div>
            <div className="bg-[var(--bg-surface-2)] px-4 py-2 flex items-center justify-between text-[11px] font-mono font-semibold text-[var(--status-critical)] border-b border-[var(--border-subtle)]">
              <span>ASKS (SELL)</span>
              <span className="text-[var(--text-secondary)] font-normal text-[10px]">
                Price / Cumulative Vol
              </span>
            </div>
            <table className="dense-table" aria-label="Asks Ladder">
              <thead>
                <tr>
                  <th className="text-left text-[var(--status-critical)]">Ask Price</th>
                  <th className="text-left">Cumul Depth</th>
                  <th className="text-right">Size</th>
                  <th className="w-16 text-right">Orders</th>
                </tr>
              </thead>
              <tbody>
                {asks.map((row, idx) => {
                  const depthPct = Math.min(100, Math.round((row.depth / maxVisibleDepth) * 100));
                  return (
                    <tr key={row.price} className="ladder-cell transition-state hover:bg-[var(--bg-surface-2)]">
                      <td className="text-left font-mono font-bold text-[var(--status-critical)] relative">
                        <div className="depth-bar-ask" style={{ width: `${depthPct}%` }} />
                        <span className="relative z-10">{fixed(row.price)}</span>
                      </td>
                      <td className="text-left font-mono text-[var(--text-secondary)] text-[11px]">
                        {row.depth.toLocaleString()}
                      </td>
                      <td className="text-right font-mono font-medium">{row.qty.toLocaleString()}</td>
                      <td className="text-right text-[11px] text-[var(--text-secondary)] font-mono">
                        {row.orders} <span className="text-[9px] text-[var(--text-secondary)]/50">/16</span>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        </div>

        {/* LOB Architecture Footnote */}
        <div className="p-3 bg-[var(--bg-surface-2)] border-t border-[var(--border-subtle)] text-[11px] text-[var(--text-secondary)] flex items-center justify-between flex-wrap gap-2">
          <span>
            Data Source: <code className="font-mono text-[var(--text-primary)]">luv_lob.hpp (PriceLevel slab)</code> · 512 Symbols · 1024 Levels/Side · FIFO Cache-Aligned Matching
          </span>
          <div className="flex items-center gap-2">
            <Button
              size="sm"
              variant="outline"
              className="text-[10px] h-6 px-2"
              onClick={() => setSimulatedFeedStalled(!simulatedFeedStalled)}
            >
              Toggle Feed Stall Sim
            </Button>
          </div>
        </div>
      </div>
    </div>
  );
}
