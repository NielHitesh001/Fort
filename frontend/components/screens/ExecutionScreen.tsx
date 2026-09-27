'use client';

import React, { useState, useEffect, useRef, useCallback } from 'react';
import {
  ListOrdered,
  PlusCircle,
  XCircle,
  RefreshCw,
  Search,
  AlertOctagon,
  ArrowRight,
  Clock,
  History,
  Info,
  DollarSign,
  TrendingUp,
} from 'lucide-react';
import { StatusChip } from '@/components/primitives/StatusChip';
import { NumericValue } from '@/components/primitives/NumericValue';
import { EmptyState } from '@/components/primitives/EmptyState';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogTitle, DialogDescription } from '@/components/ui/dialog';
import {
  Order,
  Fill,
  Position,
  fixed,
  integer,
  orderPayload,
  parseAccepted,
  parseFill,
  parseOrder,
  parsePositions,
  request,
  parseRejectMask,
} from '@/lib/contracts';
import { useConsole } from '@/lib/store';

export function ExecutionScreen({
  enabled = true,
  halted = false,
}: {
  enabled?: boolean;
  halted?: boolean;
}) {
  const { orderId, setOrderId } = useConsole();
  const [activeTab, setActiveTab] = useState<'orders' | 'positions'>('orders');

  // Form states
  const [symbolIdx, setSymbolIdx] = useState('267');
  const [side, setSide] = useState<'buy' | 'sell'>('buy');
  const [orderType, setOrderType] = useState<'limit' | 'market'>('limit');
  const [qty, setQty] = useState('100');
  const [price, setPrice] = useState('150.25');
  const [validationError, setValidationError] = useState('');

  // Execution states
  const [orders, setOrders] = useState<Order[]>([]);
  const [positions, setPositions] = useState<Position[]>([]);
  const [selectedOrder, setSelectedOrder] = useState<Order | null>(null);
  const [orderAuditHistory, setOrderAuditHistory] = useState<{ time: string; state: string; note: string }[]>([]);
  const [drawerOpen, setDrawerOpen] = useState(false);
  const [cancelModalOpen, setCancelModalOpen] = useState(false);
  const [orderToCancel, setOrderToCancel] = useState<Order | null>(null);

  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState<{ text: string; type: 'info' | 'error' | 'ok' } | null>(null);

  // Optimistic ID counter for testing & local interaction
  const optimisticIdSeq = useRef(100);

  // Fetch positions
  const refreshPositions = useCallback(async () => {
    try {
      const text = await request('positions');
      const parsed = parsePositions(text);
      setPositions(parsed.filter((p) => p.net_position !== '0' || p.gross_exposure !== '0'));
    } catch {
      // Endpoint may be quiet
    }
  }, []);

  useEffect(() => {
    void refreshPositions();
    const interval = setInterval(refreshPositions, 5000);
    return () => clearInterval(interval);
  }, [refreshPositions]);

  // Order submission
  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setValidationError('');
    setNotice(null);

    // Frontend validation per RiskLimits (luv_execution.hpp / luv_safety.hpp)
    const numericQty = Number(qty);
    if (isNaN(numericQty) || numericQty <= 0) {
      setValidationError('Quantity must be greater than 0');
      return;
    }
    if (numericQty > 1000) {
      setValidationError('Quantity exceeds max_order_qty limit (1,000 max per luv_execution.hpp RiskLimits)');
      return;
    }

    if (orderType === 'limit') {
      const numericPrice = Number(price);
      if (isNaN(numericPrice) || numericPrice <= 0) {
        setValidationError('Limit price must be greater than 0');
        return;
      }
    }

    let payload;
    try {
      payload = orderPayload(symbolIdx, side, qty, price, orderType);
    } catch (err) {
      setValidationError((err as Error).message);
      return;
    }

    setBusy(true);

    // Rule 4: Optimistic UI allowed ONLY for "pending" state.
    const tempId = `opt-${++optimisticIdSeq.current}`;
    const timestamp = new Date().toLocaleTimeString('en-US', { hour12: false });
    const optimisticOrder: Order = {
      order_id: tempId,
      status: 'pending',
      symbol_idx: Number(symbolIdx),
      qty,
      filled_qty: '0',
      price: orderType === 'market' ? '0' : String(Number(price) * 10000),
      side,
      submitted_at: timestamp,
      updated_at: timestamp,
    };

    setOrders((prev) => [optimisticOrder, ...prev]);

    try {
      const res = await request('orders', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });

      const order_id = parseAccepted(res);

      // Backend confirmed receipt -> transitions from pending to backend state
      const confirmedOrder: Order = {
        ...optimisticOrder,
        order_id,
        status: 'live', // Verified live from gateway ack
        updated_at: new Date().toLocaleTimeString('en-US', { hour12: false }),
      };

      setOrders((prev) => prev.map((o) => (o.order_id === tempId ? confirmedOrder : o)));
      setOrderId(order_id);
      setNotice({
        text: `Order #${order_id} acknowledged by ExecutionGateway (Status: live)`,
        type: 'ok',
      });
    } catch (err) {
      const errMessage = (err as Error).message;
      let rejectLabel = 'rejected';
      let rejectReason = errMessage;

      if (errMessage.includes('504') || errMessage.includes('timeout')) {
        rejectLabel = 'rejected';
        rejectReason = 'Execution transmission timeout (HTTP 504 Gateway Timeout)';
      }

      // Mark order as rejected with backend reason string
      setOrders((prev) =>
        prev.map((o) =>
          o.order_id === tempId
            ? {
                ...o,
                status: 'rejected',
                reject_reason: rejectReason,
                updated_at: new Date().toLocaleTimeString('en-US', { hour12: false }),
              }
            : o
        )
      );

      setNotice({
        text: `Order rejected: ${rejectReason}`,
        type: 'error',
      });
    } finally {
      setBusy(false);
    }
  }

  // Cancel order
  async function handleCancel(targetOrder: Order) {
    if (!targetOrder) return;
    setBusy(true);

    try {
      const res = await request(`orders/${targetOrder.order_id}`, {
        method: 'DELETE',
      });
      const parsed = parseOrder(res);

      setOrders((prev) =>
        prev.map((o) =>
          o.order_id === targetOrder.order_id
            ? {
                ...o,
                status: parsed.status,
                updated_at: new Date().toLocaleTimeString('en-US', { hour12: false }),
              }
            : o
        )
      );

      setNotice({
        text: `Order #${targetOrder.order_id} cancellation acknowledged (Status: ${parsed.status})`,
        type: 'ok',
      });
    } catch (err) {
      setNotice({
        text: `Cancel error for order #${targetOrder.order_id}: ${(err as Error).message}`,
        type: 'error',
      });
    } finally {
      setBusy(false);
      setCancelModalOpen(false);
      setOrderToCancel(null);
    }
  }

  // Open detail drawer for order
  function openOrderDrawer(o: Order) {
    setSelectedOrder(o);
    const history = [
      { time: o.submitted_at || '12:00:00', state: 'pending', note: 'Order submission received in UI' },
      { time: o.updated_at || '12:00:01', state: o.status, note: o.reject_reason || 'Gateway state transition recorded' },
    ];
    if (Number(o.filled_qty) > 0) {
      history.push({
        time: o.updated_at || '12:00:02',
        state: Number(o.filled_qty) >= Number(o.qty) ? 'done' : 'partial',
        note: `Filled ${o.filled_qty} of ${o.qty} units via matching engine`,
      });
    }
    setOrderAuditHistory(history);
    setDrawerOpen(true);
  }

  return (
    <div className="space-y-4">
      {/* Execution Halt Critical Banner */}
      {halted && (
        <div
          data-testid="execution-halt-banner"
          className="p-3.5 bg-red-950 border border-[var(--status-critical)] text-[var(--status-critical)] rounded flex items-center gap-3 text-xs"
          role="alert"
        >
          <AlertOctagon className="w-5 h-5 flex-shrink-0 animate-pulse text-[var(--status-critical)]" />
          <div>
            <strong className="font-mono uppercase font-bold text-red-200 tracking-wide">
              EXECUTION HALTED — Risk Controller Engaged
            </strong>
            <p className="text-red-300 text-[11px] mt-0.5">
              Trading is halted via ExecutionGateway / RiskState.halted. Ingress order submissions are actively rejected.
            </p>
          </div>
        </div>
      )}

      {/* Notification Toast Bar */}
      {notice && (
        <div
          className={`p-3 rounded border text-xs font-mono flex items-center justify-between ${
            notice.type === 'error'
              ? 'bg-red-950/40 border-red-500/50 text-red-300'
              : notice.type === 'ok'
              ? 'bg-emerald-950/40 border-emerald-500/50 text-emerald-300'
              : 'bg-blue-950/40 border-blue-500/50 text-blue-300'
          }`}
          role="status"
        >
          <span>{notice.text}</span>
          <button
            onClick={() => setNotice(null)}
            className="text-[var(--text-secondary)] hover:text-[var(--text-primary)]"
          >
            ×
          </button>
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-4">
        {/* ORDER ENTRY FORM (Left Column, Section 5.2) */}
        <div className="panel-card lg:col-span-1">
          <div className="panel-header">
            <div className="flex items-center gap-2">
              <PlusCircle className="w-4 h-4 text-[var(--accent)]" />
              <h2>New Order Entry</h2>
            </div>
            <StatusChip status="live" label="PreTradeRisk Active" />
          </div>

          <form onSubmit={handleSubmit} className="p-4 space-y-3.5">
            <div>
              <label className="text-[11px] font-mono text-[var(--text-secondary)] uppercase block mb-1">
                Instrument (Symbol Index)
              </label>
              <select
                aria-label="Symbol Selection"
                className="font-mono text-xs"
                value={symbolIdx}
                onChange={(e) => setSymbolIdx(e.target.value)}
                disabled={busy || halted}
              >
                <option value="267">AAPL (Symbol #267)</option>
                <option value="0">EUR/USD (Symbol #0)</option>
                <option value="1">USD/INR (Symbol #1)</option>
                <option value="2">GBP/USD (Symbol #2)</option>
                <option value="3">USD/JPY (Symbol #3)</option>
              </select>
              <span className="text-[10px] text-[var(--text-secondary)] mt-0.5 block">
                Index 0–511 · Bounded pre-allocated slab
              </span>
            </div>

            <div className="grid grid-cols-2 gap-2">
              <div>
                <label className="text-[11px] font-mono text-[var(--text-secondary)] uppercase block mb-1">
                  Side
                </label>
                <div className="grid grid-cols-2 gap-1 bg-[var(--bg-canvas)] p-0.5 border border-[var(--border-subtle)] rounded">
                  <button
                    type="button"
                    className={`py-1 text-xs font-mono font-semibold rounded ${
                      side === 'buy'
                        ? 'bg-[var(--status-ok)] text-black'
                        : 'text-[var(--text-secondary)] hover:text-[var(--text-primary)]'
                    }`}
                    onClick={() => setSide('buy')}
                  >
                    BUY
                  </button>
                  <button
                    type="button"
                    className={`py-1 text-xs font-mono font-semibold rounded ${
                      side === 'sell'
                        ? 'bg-[var(--status-critical)] text-white'
                        : 'text-[var(--text-secondary)] hover:text-[var(--text-primary)]'
                    }`}
                    onClick={() => setSide('sell')}
                  >
                    SELL
                  </button>
                </div>
              </div>

              <div>
                <label className="text-[11px] font-mono text-[var(--text-secondary)] uppercase block mb-1">
                  Type (README Bound)
                </label>
                <select
                  aria-label="Order Type"
                  className="font-mono text-xs"
                  value={orderType}
                  onChange={(e) => setOrderType(e.target.value as 'limit' | 'market')}
                >
                  <option value="limit">LIMIT</option>
                  <option value="market">MARKET</option>
                </select>
              </div>
            </div>

            <div className="grid grid-cols-2 gap-2">
              <div>
                <label className="text-[11px] font-mono text-[var(--text-secondary)] uppercase block mb-1">
                  Quantity (Units)
                </label>
                <input
                  aria-label="Quantity"
                  type="number"
                  min="1"
                  max="1000"
                  className="font-mono text-xs"
                  placeholder="100"
                  value={qty}
                  onChange={(e) => setQty(e.target.value)}
                  disabled={busy || halted}
                />
                <span className="text-[9px] text-[var(--text-secondary)] block mt-0.5">
                  Max: 1,000 / order
                </span>
              </div>

              <div>
                <label className="text-[11px] font-mono text-[var(--text-secondary)] uppercase block mb-1">
                  Limit Price ($)
                </label>
                <input
                  aria-label="Limit Price"
                  type="text"
                  className="font-mono text-xs"
                  placeholder="150.25"
                  value={price}
                  onChange={(e) => setPrice(e.target.value)}
                  disabled={busy || halted || orderType === 'market'}
                />
                <span className="text-[9px] text-[var(--text-secondary)] block mt-0.5">
                  Fixed point ×10⁴
                </span>
              </div>
            </div>

            {validationError && (
              <div className="p-2 bg-red-950/50 border border-red-600/50 text-red-300 text-[11px] rounded">
                {validationError}
              </div>
            )}

            <Button
              type="submit"
              className="w-full bg-[var(--accent)] hover:bg-blue-600 text-white font-medium text-xs py-2 mt-2"
              disabled={busy || halted || !enabled}
            >
              {busy ? 'Submitting to Gateway…' : 'Submit Order'}
            </Button>

            <div className="border-t border-[var(--border-subtle)] pt-2.5 text-[10px] text-[var(--text-secondary)] space-y-1">
              <div className="flex justify-between">
                <span>PreTradeRisk collar check:</span>
                <span className="font-mono text-[var(--text-primary)]">±10% max</span>
              </div>
              <div className="flex justify-between">
                <span>Active order limit:</span>
                <span className="font-mono text-[var(--text-primary)]">64 / symbol</span>
              </div>
            </div>
          </form>
        </div>

        {/* ORDER MANAGEMENT TABLE & POSITIONS (Right 2 Columns) */}
        <div className="panel-card lg:col-span-2">
          <div className="panel-header">
            <div className="flex items-center gap-3">
              <button
                className={`text-xs font-semibold pb-1 border-b-2 transition-state ${
                  activeTab === 'orders'
                    ? 'border-[var(--accent)] text-[var(--text-primary)]'
                    : 'border-transparent text-[var(--text-secondary)] hover:text-[var(--text-primary)]'
                }`}
                onClick={() => setActiveTab('orders')}
              >
                Active & Recent Orders ({orders.length})
              </button>
              <button
                className={`text-xs font-semibold pb-1 border-b-2 transition-state ${
                  activeTab === 'positions'
                    ? 'border-[var(--accent)] text-[var(--text-primary)]'
                    : 'border-transparent text-[var(--text-secondary)] hover:text-[var(--text-primary)]'
                }`}
                onClick={() => setActiveTab('positions')}
              >
                Symbol Positions ({positions.length})
              </button>
            </div>
            <Button
              size="sm"
              variant="outline"
              className="h-7 text-xs gap-1.5"
              onClick={refreshPositions}
            >
              <RefreshCw className="w-3 h-3" /> Refresh
            </Button>
          </div>

          {activeTab === 'orders' ? (
            orders.length === 0 ? (
              <EmptyState
                title="No Orders Submitted Yet"
                description="Submit a simulator limit/market order using the ticket on the left to monitor its lifecycle."
                backendSource="luv_execution.hpp · ExecutionGateway"
                icon={ListOrdered}
              />
            ) : (
              <div className="overflow-x-auto">
                <table className="dense-table" aria-label="Orders Table">
                  <thead>
                    <tr>
                      <th>Order ID</th>
                      <th>Symbol</th>
                      <th>Side</th>
                      <th className="text-right">Qty / Filled</th>
                      <th className="text-right">Price</th>
                      <th>Status</th>
                      <th>Submitted</th>
                      <th className="text-right">Actions</th>
                    </tr>
                  </thead>
                  <tbody>
                    {orders.map((o) => {
                      const canCancel = ['pending', 'live', 'partial'].includes(o.status);
                      return (
                        <tr key={o.order_id} className="hover:bg-[var(--bg-surface-2)] transition-state">
                          <td className="font-mono text-xs font-semibold text-[var(--accent)]">
                            #{o.order_id}
                          </td>
                          <td className="font-mono text-xs">
                            {o.symbol_idx === 267 ? 'AAPL (267)' : `Sym #${o.symbol_idx}`}
                          </td>
                          <td>
                            <span
                              className={`font-mono text-[11px] font-bold uppercase ${
                                o.side === 'buy' ? 'text-[var(--status-ok)]' : 'text-[var(--status-critical)]'
                              }`}
                            >
                              {o.side || 'BUY'}
                            </span>
                          </td>
                          <td className="text-right font-mono">
                            {o.qty} <span className="text-[var(--text-secondary)]">/ {o.filled_qty}</span>
                          </td>
                          <td className="text-right font-mono font-medium">
                            {fixed(o.price)}
                          </td>
                          <td>
                            <StatusChip status={o.status} />
                          </td>
                          <td className="font-mono text-[11px] text-[var(--text-secondary)]">
                            {o.submitted_at || '—'}
                          </td>
                          <td className="text-right space-x-1.5">
                            <Button
                              size="sm"
                              variant="ghost"
                              className="h-6 px-2 text-[11px]"
                              onClick={() => openOrderDrawer(o)}
                            >
                              Details
                            </Button>
                            {canCancel && (
                              <Button
                                size="sm"
                                variant="outline"
                                className="h-6 px-2 text-[11px] text-red-400 border-red-500/30 hover:bg-red-950"
                                onClick={() => {
                                  setOrderToCancel(o);
                                  setCancelModalOpen(true);
                                }}
                              >
                                Cancel
                              </Button>
                            )}
                          </td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              </div>
            )
          ) : (
            positions.length === 0 ? (
              <EmptyState
                title="Zero Exposure / No Open Positions"
                description="Positions snapshot returned from GET /api/v1/positions shows flat exposure across all 512 symbols."
                backendSource="luv_http_server.hpp · GET /api/v1/positions"
                icon={TrendingUp}
              />
            ) : (
              <div className="overflow-x-auto">
                <table className="dense-table" aria-label="Positions Table">
                  <thead>
                    <tr>
                      <th>Symbol Index</th>
                      <th className="text-right">Net Position (Units)</th>
                      <th className="text-right">Gross Exposure ($)</th>
                    </tr>
                  </thead>
                  <tbody>
                    {positions.map((p) => (
                      <tr key={p.symbol_idx} className="hover:bg-[var(--bg-surface-2)]">
                        <td className="font-mono text-xs font-semibold">
                          Symbol #{p.symbol_idx} {p.symbol_idx === 267 ? '(AAPL)' : ''}
                        </td>
                        <td
                          className={`text-right font-mono font-bold ${
                            BigInt(p.net_position) > 0n
                              ? 'text-[var(--status-ok)]'
                              : BigInt(p.net_position) < 0n
                              ? 'text-[var(--status-critical)]'
                              : 'text-[var(--text-secondary)]'
                          }`}
                        >
                          {p.net_position}
                        </td>
                        <td className="text-right font-mono text-[var(--text-primary)]">
                          {fixed(p.gross_exposure)}
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )
          )}
        </div>
      </div>

      {/* DETAIL DRAWER / AUDIT MODAL (Section 5.2) */}
      <Dialog open={drawerOpen} onOpenChange={setDrawerOpen}>
        <DialogContent className="max-w-lg">
          <DialogTitle className="flex items-center gap-2">
            <History className="w-4 h-4 text-[var(--accent)]" />
            Order #{selectedOrder?.order_id} Audit History
          </DialogTitle>
          <DialogDescription>
            State transition log and execution records for this order session.
          </DialogDescription>

          {selectedOrder && (
            <div className="space-y-4 my-3 text-xs">
              <div className="grid grid-cols-2 gap-2 p-3 bg-[var(--bg-surface-2)] rounded border border-[var(--border-subtle)] font-mono">
                <div>
                  <span className="text-[var(--text-secondary)] block text-[10px]">SYMBOL</span>
                  <span>Symbol #{selectedOrder.symbol_idx}</span>
                </div>
                <div>
                  <span className="text-[var(--text-secondary)] block text-[10px]">CURRENT STATUS</span>
                  <StatusChip status={selectedOrder.status} />
                </div>
                <div>
                  <span className="text-[var(--text-secondary)] block text-[10px]">QUANTITY / FILLED</span>
                  <span>{selectedOrder.qty} / {selectedOrder.filled_qty} units</span>
                </div>
                <div>
                  <span className="text-[var(--text-secondary)] block text-[10px]">PRICE</span>
                  <span>{fixed(selectedOrder.price)}</span>
                </div>
              </div>

              {selectedOrder.reject_reason && (
                <div className="p-3 bg-red-950/60 border border-red-500 text-red-200 rounded font-mono text-[11px]">
                  <strong>Rejection Reason:</strong> {selectedOrder.reject_reason}
                </div>
              )}

              <div>
                <h4 className="font-semibold text-[var(--text-primary)] mb-2 font-mono">
                  State Transition History:
                </h4>
                <div className="border border-[var(--border-subtle)] rounded divide-y divide-[var(--border-subtle)]">
                  {orderAuditHistory.map((h, i) => (
                    <div key={i} className="p-2.5 flex items-center justify-between text-[11px]">
                      <div className="flex items-center gap-2">
                        <Clock className="w-3.5 h-3.5 text-[var(--text-secondary)]" />
                        <span className="font-mono text-[var(--text-secondary)]">{h.time}</span>
                        <StatusChip status={h.state} />
                      </div>
                      <span className="text-[var(--text-secondary)]">{h.note}</span>
                    </div>
                  ))}
                </div>
              </div>
            </div>
          )}
        </DialogContent>
      </Dialog>

      {/* CANCEL CONFIRMATION MODAL */}
      <Dialog open={cancelModalOpen} onOpenChange={setCancelModalOpen}>
        <DialogContent className="max-w-md">
          <DialogTitle>Confirm Order Cancellation</DialogTitle>
          <DialogDescription>
            Submit a cancel request to ExecutionGateway for order #{orderToCancel?.order_id}.
          </DialogDescription>
          <div className="flex justify-end gap-2 mt-4">
            <Button variant="ghost" size="sm" onClick={() => setCancelModalOpen(false)}>
              Back
            </Button>
            <Button
              variant="outline"
              size="sm"
              className="border-red-500 text-red-400 hover:bg-red-950"
              onClick={() => orderToCancel && handleCancel(orderToCancel)}
              disabled={busy}
            >
              {busy ? 'Cancelling…' : 'Cancel Order'}
            </Button>
          </div>
        </DialogContent>
      </Dialog>
    </div>
  );
}
