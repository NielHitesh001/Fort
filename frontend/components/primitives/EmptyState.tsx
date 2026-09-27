import React from 'react';
import { Database, CircleSlash, FileCode2 } from 'lucide-react';

interface EmptyStateProps {
  title: string;
  description?: React.ReactNode;
  backendSource?: string;
  icon?: React.ComponentType<{ className?: string }>;
  className?: string;
  action?: React.ReactNode;
}

export function EmptyState({
  title,
  description,
  backendSource,
  icon: Icon = Database,
  className = '',
  action,
}: EmptyStateProps) {
  return (
    <div
      className={`flex flex-col items-center justify-center p-8 text-center rounded border border-[var(--border-subtle)] bg-[var(--bg-surface)] min-h-[180px] ${className}`}
    >
      <div className="w-10 h-10 rounded-full bg-[var(--bg-surface-2)] flex items-center justify-center text-[var(--text-secondary)] mb-3">
        <Icon className="w-5 h-5" />
      </div>
      <h3 className="text-[13px] font-medium text-[var(--text-primary)] mb-1">{title}</h3>
      {description && (
        <p className="text-[11px] text-[var(--text-secondary)] max-w-md leading-relaxed mb-3">
          {description}
        </p>
      )}
      {backendSource && (
        <div className="inline-flex items-center gap-1.5 px-2 py-0.5 rounded bg-[var(--bg-surface-2)] border border-[var(--border-subtle)] text-[10px] font-mono text-[var(--text-secondary)] mb-3">
          <FileCode2 className="w-3 h-3 text-[var(--accent)]" />
          <span>Source: {backendSource}</span>
        </div>
      )}
      {action && <div className="mt-1">{action}</div>}
    </div>
  );
}
