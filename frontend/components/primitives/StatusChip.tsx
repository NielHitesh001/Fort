import React from 'react';
import {
  CheckCircle2,
  AlertTriangle,
  XCircle,
  Clock,
  Radio,
  ShieldAlert,
  HelpCircle,
} from 'lucide-react';
import { OrderStatus } from '@/lib/contracts';

export type StatusVariant = 'ok' | 'warn' | 'critical' | 'neutral';

interface StatusChipProps {
  status?: OrderStatus | string;
  variant?: StatusVariant;
  label?: string;
  className?: string;
  showIcon?: boolean;
}

export function getStatusVariant(status?: string): { variant: StatusVariant; label: string; icon: React.ReactNode } {
  if (!status) {
    return { variant: 'neutral', label: '—', icon: <HelpCircle className="w-3 h-3" /> };
  }

  const normalized = status.toLowerCase();

  switch (normalized) {
    // Healthy / OK statuses
    case 'live':
    case 'done':
    case 'filled':
    case 'accepted':
    case 'connected':
    case 'healthy':
    case 'kclosed':
    case 'normal':
    case '200':
    case 'ok':
      return {
        variant: 'ok',
        label: status === 'kclosed' ? 'kClosed (Normal)' : status,
        icon: <CheckCircle2 className="w-3 h-3 text-[var(--status-ok)]" />,
      };

    // Warning statuses
    case 'pending':
    case 'partial':
    case 'reconnecting':
    case 'warning':
    case 'khalfopen':
    case 'shedload':
      return {
        variant: 'warn',
        label: status === 'khalfopen' ? 'kHalfOpen (Probe)' : status,
        icon: <Clock className="w-3 h-3 text-[var(--status-warn)]" />,
      };

    // Critical / Error statuses
    case 'rejected':
    case 'cancelled':
    case 'failed':
    case 'stalled':
    case 'tripped':
    case 'halted':
    case 'kopen':
    case 'emergencyhalt':
    case 'exhausted':
    case 'timeout':
    case 'unavailable':
      return {
        variant: 'critical',
        label: status === 'kopen' ? 'kOpen (Halted)' : status,
        icon: <XCircle className="w-3 h-3 text-[var(--status-critical)]" />,
      };

    case 'not_found':
      return {
        variant: 'neutral',
        label: 'not_found',
        icon: <HelpCircle className="w-3 h-3 text-[var(--text-secondary)]" />,
      };

    default:
      return {
        variant: 'neutral',
        label: status,
        icon: <Radio className="w-3 h-3 text-[var(--text-secondary)]" />,
      };
  }
}

export function StatusChip({
  status,
  variant: forcedVariant,
  label: forcedLabel,
  className = '',
  showIcon = true,
}: StatusChipProps) {
  const resolved = getStatusVariant(status);
  const variant = forcedVariant || resolved.variant;
  const label = forcedLabel || resolved.label;

  const colorStyles = {
    ok: 'text-[var(--status-ok)] border-[var(--status-ok)]/40 bg-[var(--status-ok)]/10',
    warn: 'text-[var(--status-warn)] border-[var(--status-warn)]/40 bg-[var(--status-warn)]/10',
    critical: 'text-[var(--status-critical)] border-[var(--status-critical)]/40 bg-[var(--status-critical)]/10',
    neutral: 'text-[var(--text-secondary)] border-[var(--border-subtle)] bg-[var(--bg-surface-2)]/50',
  }[variant];

  return (
    <span
      className={`inline-flex items-center gap-1.5 px-2 py-0.5 rounded text-[11px] font-mono uppercase tracking-wider border font-medium ${colorStyles} ${className}`}
      role="status"
    >
      {showIcon && resolved.icon}
      <span>{label}</span>
    </span>
  );
}
