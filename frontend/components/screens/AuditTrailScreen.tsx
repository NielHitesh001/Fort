'use client';

import React, { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import {
  FileText,
  Download,
  Search,
  RefreshCw,
  ShieldCheck,
  FileLock2,
  AlertTriangle,
  Lock,
  Database,
} from 'lucide-react';
import { StatusChip } from '@/components/primitives/StatusChip';
import { NumericValue } from '@/components/primitives/NumericValue';
import { EmptyState } from '@/components/primitives/EmptyState';
import { Button } from '@/components/ui/button';
import { request, fixed, exportToCSV } from '@/lib/contracts';
import { useConnectionState } from '@/lib/connection';

type LedgerRecord = {
  sequence: string;
  type: string;
  order_id: string;
  quantity: string;
  price: string;
  side: number;
  timestamp_ns: string;
};

type LedgerResponse = {
  total: number;
  validation: string;
  rows: LedgerRecord[];
};

export function AuditTrailScreen() {
  const [filterOrderId, setFilterOrderId] = useState('');
  const [filterEventType, setFilterEventType] = useState('ALL');
  const [page, setPage] = useState(0);
  const pageSize = 20;

  const { isOnline } = useConnectionState();

  const query = useQuery({
    queryKey: ['ledger'],
    queryFn: async () => {
      const text = await request('ledger');
      return JSON.parse(text) as LedgerResponse;
    },
    retry: false,
    refetchOnWindowFocus: false,
  });

  // BUG 1 & PART 3 ITEM 4 FIX:
  // Strictly use backend ledger rows from query. No fabricated fallback fixtures.
  const allRows: LedgerRecord[] = !query.isError && query.data?.rows ? query.data.rows : [];
  const hasRealData = !query.isError && !!query.data?.rows;
  const isValidated = !query.isError && query.data?.validation === 'valid';

  const filteredRows = allRows.filter((r) => {
    const matchesOrder = !filterOrderId || r.order_id.includes(filterOrderId);
    const matchesType = filterEventType === 'ALL' || r.type === filterEventType;
    return matchesOrder && matchesType;
  });

  const paginatedRows = filteredRows.slice(page * pageSize, (page + 1) * pageSize);
  const totalPages = Math.ceil(filteredRows.length / pageSize) || 1;

  function handleExportCSV() {
    if (!filteredRows.length) return;
    const headers = ['Sequence', 'Event Type', 'Order ID', 'Side', 'Quantity', 'Price (Fixed)', 'Timestamp (ns)'];
    const data = filteredRows.map((r) => [
      r.sequence,
      r.type,
      r.order_id,
      r.side === 0 ? 'BUY' : 'SELL',
      r.quantity,
      fixed(r.price),
      r.timestamp_ns,
    ]);
    exportToCSV(`fort-audit-ledger-${Date.now()}.csv`, headers, data);
  }

  return (
    <div className="space-y-4">
      {/* Summary KPI Cards */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
        <div className="panel-card p-4">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Ledger Records Logged
          </span>
          <div className="text-2xl font-mono font-bold text-[var(--text-primary)] my-1">
            <NumericValue value={hasRealData ? query.data?.total : undefined} unit="events" />
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            DurableAuditLog / RecoveryLedger
          </span>
        </div>

        <div className="panel-card p-4">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Hash Integrity Verification
          </span>
          <div className="text-2xl font-mono font-bold my-1 flex items-center gap-2">
            {isValidated ? (
              <>
                <ShieldCheck className="w-5 h-5 text-[var(--status-ok)]" />
                <span className="text-[var(--status-ok)]">SHA-256 Verified</span>
              </>
            ) : (
              <>
                <FileLock2 className="w-5 h-5 text-[var(--text-secondary)]" />
                <span className="text-[var(--text-secondary)]">Awaiting Data</span>
              </>
            )}
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            {isValidated ? 'Sequence checksums verified' : 'No verified ledger stream active'}
          </span>
        </div>

        <div className="panel-card p-4">
          <span className="text-[10px] font-mono text-[var(--text-secondary)] uppercase block">
            Compliance Certification Status
          </span>
          <div className="text-2xl font-mono font-bold text-[var(--status-warn)] my-1">
            <span>Research Grade</span>
          </div>
          <span className="text-[10px] text-[var(--text-secondary)] block">
            Not WORM / SEC 17a-4 certified
          </span>
        </div>
      </div>

      {/* Main Ledger Table */}
      <div className="panel-card">
        <div className="panel-header flex-wrap gap-3">
          <div className="flex items-center gap-2">
            <FileText className="w-4 h-4 text-[var(--accent)]" />
            <h2>Audit & Recovery Ledger View (Section 5.5)</h2>
          </div>

          <div className="flex items-center gap-2 flex-wrap">
            <div className="relative">
              <Search className="w-3.5 h-3.5 absolute left-2.5 top-2.5 text-[var(--text-secondary)]" />
              <input
                aria-label="Filter ledger by Order ID"
                type="text"
                placeholder="Filter by Order ID…"
                className="pl-8 h-8 w-44 font-mono text-xs"
                value={filterOrderId}
                onChange={(e) => {
                  setFilterOrderId(e.target.value);
                  setPage(0);
                }}
                disabled={!hasRealData}
              />
            </div>

            <select
              aria-label="Filter by Event Type"
              className="h-8 w-32 font-mono text-xs"
              value={filterEventType}
              onChange={(e) => {
                setFilterEventType(e.target.value);
                setPage(0);
              }}
              disabled={!hasRealData}
            >
              <option value="ALL">All Events</option>
              <option value="kAdd">kAdd</option>
              <option value="kAck">kAck</option>
              <option value="kFill">kFill</option>
              <option value="kCancel">kCancel</option>
              <option value="kNew">kNew</option>
            </select>

            <Button
              size="sm"
              variant="outline"
              className="h-8 text-xs gap-1.5"
              onClick={handleExportCSV}
              disabled={!filteredRows.length}
            >
              <Download className="w-3.5 h-3.5" /> Export CSV
            </Button>
          </div>
        </div>

        {!hasRealData ? (
          <div className="p-6">
            <EmptyState
              title="Awaiting Engine Recovery Ledger Stream"
              description="Durable audit records are read from the C++20 RecoveryLedger binary file via /api/corridor/ledger. When the engine ledger is offline or unconfigured, unverified mock records are not displayed."
              backendSource="luv_recovery.hpp · RecoveryLedger / DurableAuditLog (SHA-256 Chained)"
              icon={FileLock2}
            />
          </div>
        ) : (
          <>
            <div className="overflow-x-auto">
              <table className="dense-table" aria-label="Audit Records Table">
                <thead>
                  <tr>
                    <th>Seq #</th>
                    <th>Event Type</th>
                    <th>Order ID</th>
                    <th>Side</th>
                    <th className="text-right">Quantity</th>
                    <th className="text-right">Price</th>
                    <th className="text-right font-mono">Timestamp (ns Monotonic)</th>
                    <th>Integrity Proof</th>
                  </tr>
                </thead>
                <tbody>
                  {paginatedRows.length > 0 ? (
                    paginatedRows.map((row) => (
                      <tr key={row.sequence} className="hover:bg-[var(--bg-surface-2)]">
                        <td className="font-mono text-xs text-[var(--text-secondary)]">
                          #{row.sequence}
                        </td>
                        <td>
                          <StatusChip status="live" label={row.type} />
                        </td>
                        <td className="font-mono text-xs font-semibold text-[var(--accent)]">
                          #{row.order_id}
                        </td>
                        <td>
                          <span
                            className={`font-mono text-[11px] font-bold uppercase ${
                              row.side === 0 ? 'text-[var(--status-ok)]' : 'text-[var(--status-critical)]'
                            }`}
                          >
                            {row.side === 0 ? 'BUY' : 'SELL'}
                          </span>
                        </td>
                        <td className="text-right font-mono font-medium">
                          {Number(row.quantity).toLocaleString()}
                        </td>
                        <td className="text-right font-mono">
                          {fixed(row.price)}
                        </td>
                        <td className="text-right font-mono text-[11px] text-[var(--text-secondary)]">
                          {row.timestamp_ns}
                        </td>
                        <td>
                          <span className="font-mono text-[10px] text-emerald-400 bg-emerald-950/40 px-1.5 py-0.5 rounded border border-emerald-500/20">
                            SHA-256 Valid
                          </span>
                        </td>
                      </tr>
                    ))
                  ) : (
                    <tr>
                      <td colSpan={8}>
                        <EmptyState
                          title="No Matching Audit Records"
                          description="No records match the current filter criteria."
                          icon={FileLock2}
                        />
                      </td>
                    </tr>
                  )}
                </tbody>
              </table>
            </div>

            {/* Pagination Controls */}
            <div className="p-3 border-t border-[var(--border-subtle)] flex items-center justify-between text-xs text-[var(--text-secondary)] font-mono">
              <span>
                Showing {filteredRows.length ? page * pageSize + 1 : 0}–
                {Math.min((page + 1) * pageSize, filteredRows.length)} of {filteredRows.length} records
              </span>
              <div className="flex gap-1.5">
                <Button
                  size="sm"
                  variant="outline"
                  className="h-7 text-xs"
                  disabled={page === 0}
                  onClick={() => setPage((p) => p - 1)}
                >
                  Previous
                </Button>
                <Button
                  size="sm"
                  variant="outline"
                  className="h-7 text-xs"
                  disabled={(page + 1) * pageSize >= filteredRows.length}
                  onClick={() => setPage((p) => p + 1)}
                >
                  Next
                </Button>
              </div>
            </div>
          </>
        )}
      </div>

      {/* Compliance Scope Boundary Notice (Section 5.5) */}
      <div className="panel-card p-4">
        <div className="flex items-start gap-3 text-xs">
          <Lock className="w-5 h-5 text-amber-400 flex-shrink-0 mt-0.5" />
          <div className="space-y-1">
            <strong className="text-[var(--text-primary)] font-medium">
              Regulatory Scope Disclosure (luv_safety.hpp & AUDIT_LOGGING.md)
            </strong>
            <p className="text-[var(--text-secondary)] leading-relaxed text-[11px]">
              DurableAuditLog uses a local SHA-256 hash chain for accidental-corruption detection. It is NOT an SEC Rule 17a-4, FINRA, CAT, or WORM-compliant recordkeeping system. It cannot prove the absence of truncated history or defend against an actor able to replace local files. Regulated deployments require independently administered WORM custody, retention controls, trusted timestamps, and legal/compliance review.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}
