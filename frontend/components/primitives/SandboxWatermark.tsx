import React from 'react';
import { ShieldAlert, AlertTriangle } from 'lucide-react';

interface SandboxWatermarkProps {
  mode?: 'SIMULATION' | 'PAPER' | 'LIVE';
  compact?: boolean;
}

export function SandboxWatermark({ mode = 'SIMULATION', compact = false }: SandboxWatermarkProps) {
  if (mode === 'LIVE') return null;

  if (compact) {
    return (
      <div
        data-testid="sandbox-watermark"
        className="inline-flex items-center gap-1.5 px-2 py-0.5 rounded bg-amber-950/40 border border-amber-600/40 text-amber-300 font-mono text-[10px] font-semibold tracking-wider uppercase"
      >
        <AlertTriangle className="w-3 h-3 text-amber-400" />
        <span>{mode} DATA</span>
      </div>
    );
  }

  return (
    <div
      data-testid="sandbox-watermark-banner"
      className="w-full bg-[#131c27] border-b border-[var(--border-subtle)] px-6 py-2 flex items-center justify-between text-[11px] text-[var(--text-secondary)] select-none z-10"
    >
      <div className="flex items-center gap-2">
        <span className="w-2 h-2 rounded-full bg-amber-400 animate-pulse" />
        <strong className="text-amber-300 font-mono font-semibold tracking-wide">
          {mode === 'SIMULATION' ? 'SIMULATED DATA · RESEARCH RUNTIME' : 'PAPER TRADING · SANDBOX'}
        </strong>
        <span className="text-[var(--border-subtle)]">|</span>
        <span>
          Fort is an in-process market microstructure simulator. Measurements and fills do not represent live exchange execution.
        </span>
      </div>
      <div className="hidden md:flex items-center gap-2 font-mono text-[10px] text-[var(--text-secondary)]">
        <span>NO LIVE CAPITAL CONNECTED</span>
        <span className="px-1.5 py-0.2 border border-[var(--border-subtle)] rounded bg-[var(--bg-surface-2)]">
          EXECUTION_MODE={mode}
        </span>
      </div>
    </div>
  );
}
