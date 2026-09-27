'use client';

import React from 'react';
import { FlaskConical, AlertCircle } from 'lucide-react';
import { Button } from '@/components/ui/button';

interface DemoOnlyControlProps {
  label: string;
  onClick: () => void;
  description?: string;
  className?: string;
  active?: boolean;
}

export function DemoOnlyControl({
  label,
  onClick,
  description = 'Local UI test toggle — not wired to C++20 engine control plane',
  className = '',
  active = false,
}: DemoOnlyControlProps) {
  return (
    <div
      className={`inline-flex flex-col sm:flex-row items-start sm:items-center gap-2 p-2 rounded border border-dashed border-amber-500/40 bg-amber-950/20 text-xs ${className}`}
    >
      <div className="flex items-center gap-1.5 text-amber-400 font-mono text-[10px] font-semibold tracking-wider uppercase">
        <FlaskConical className="w-3.5 h-3.5 flex-shrink-0" />
        <span>DEMO / UI-ONLY</span>
      </div>

      <div className="flex items-center gap-2 flex-wrap">
        <Button
          size="sm"
          variant="outline"
          className={`h-6 text-[11px] font-mono border-amber-500/50 hover:bg-amber-900/40 ${
            active ? 'bg-amber-500/20 text-amber-200 font-bold' : 'text-amber-300'
          }`}
          onClick={onClick}
          title={description}
        >
          {label}
        </Button>
        <span className="text-[10px] text-[var(--text-secondary)] font-mono hidden md:inline">
          ({description})
        </span>
      </div>
    </div>
  );
}
