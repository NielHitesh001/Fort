import React from 'react';

interface NumericValueProps {
  value?: number | string | bigint | null;
  unit?: string;
  precision?: number;
  highlight?: 'ok' | 'warn' | 'critical' | 'accent' | 'neutral';
  className?: string;
  prefix?: string;
  placeholder?: string;
  compact?: boolean;
}

export function NumericValue({
  value,
  unit,
  precision,
  highlight,
  className = '',
  prefix = '',
  placeholder = '—',
  compact = false,
}: NumericValueProps) {
  if (value === undefined || value === null || value === '') {
    return (
      <span className={`font-mono tabular-nums text-[var(--text-secondary)] ${className}`}>
        {placeholder}
      </span>
    );
  }

  let formatted = String(value);

  if (typeof value === 'number') {
    if (precision !== undefined) {
      formatted = value.toLocaleString('en-US', {
        minimumFractionDigits: precision,
        maximumFractionDigits: precision,
      });
    } else if (compact) {
      formatted = Intl.NumberFormat('en-US', {
        notation: 'compact',
        maximumFractionDigits: 2,
      }).format(value);
    } else {
      formatted = value.toLocaleString('en-US');
    }
  }

  const highlightClass = highlight
    ? {
        ok: 'text-[var(--status-ok)]',
        warn: 'text-[var(--status-warn)]',
        critical: 'text-[var(--status-critical)]',
        accent: 'text-[var(--accent)]',
        neutral: 'text-[var(--text-primary)]',
      }[highlight]
    : 'text-[var(--text-primary)]';

  return (
    <span className={`inline-flex items-baseline gap-1 font-mono tabular-nums ${className}`}>
      {prefix && <span className="text-[var(--text-secondary)] select-none text-[0.85em]">{prefix}</span>}
      <span className={`font-medium ${highlightClass}`}>{formatted}</span>
      {unit && <span className="text-[var(--text-secondary)] text-[0.85em] font-sans font-normal ml-0.5">{unit}</span>}
    </span>
  );
}
