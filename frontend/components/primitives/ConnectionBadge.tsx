import React from 'react';
import { Activity, Wifi, WifiOff, AlertCircle } from 'lucide-react';

export type ConnectionState = 'connected' | 'reconnecting' | 'stalled' | 'disconnected';

interface ConnectionBadgeProps {
  wsConnected?: boolean;
  healthOk?: boolean;
  feedFresh?: boolean;
  ageSeconds?: number | null;
  className?: string;
  showDetails?: boolean;
}

export function ConnectionBadge({
  wsConnected = false,
  healthOk = true,
  feedFresh = true,
  ageSeconds = null,
  className = '',
  showDetails = true,
}: ConnectionBadgeProps) {
  let state: ConnectionState = 'disconnected';
  let label = 'Offline';
  let color = 'text-[var(--status-critical)] border-[var(--status-critical)]/40 bg-[var(--status-critical)]/10';
  let Icon = WifiOff;

  if (healthOk && wsConnected && feedFresh) {
    state = 'connected';
    label = 'Connected';
    color = 'text-[var(--status-ok)] border-[var(--status-ok)]/40 bg-[var(--status-ok)]/10';
    Icon = Wifi;
  } else if (healthOk && !feedFresh) {
    state = 'stalled';
    label = 'Feed Stalled';
    color = 'text-[var(--status-critical)] border-[var(--status-critical)]/40 bg-[var(--status-critical)]/10';
    Icon = AlertCircle;
  } else if (healthOk && !wsConnected) {
    state = 'reconnecting';
    label = 'WS Disconnected';
    color = 'text-[var(--status-warn)] border-[var(--status-warn)]/40 bg-[var(--status-warn)]/10';
    Icon = Activity;
  }

  return (
    <div
      className={`inline-flex items-center gap-2 px-2.5 py-1 rounded border text-[11px] font-mono font-medium ${color} ${className}`}
      title={`Health: ${healthOk ? '200 OK' : 'Unavailable'} | WS: ${wsConnected ? 'Connected' : 'Disconnected'} | Feed: ${feedFresh ? 'Fresh' : 'Stalled'}`}
    >
      <Icon className="w-3.5 h-3.5 animate-pulse" />
      <span>{label}</span>
      {showDetails && ageSeconds !== null && ageSeconds !== undefined && (
        <span className="text-[var(--text-secondary)] border-l border-current/20 pl-1.5 ml-0.5 text-[10px]">
          {ageSeconds}s
        </span>
      )}
    </div>
  );
}
