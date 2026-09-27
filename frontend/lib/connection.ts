'use client';

import { useState, useEffect, useRef, useCallback } from 'react';
import { useQuery } from '@tanstack/react-query';
import { parseMetrics, request } from '@/lib/contracts';

export type Sample = { time: string; rate?: number; risk?: number };

export type FeedStatus = 'ingesting' | 'idle' | 'stalled' | 'offline';

export interface ConnectionState {
  isOnline: boolean;
  healthOk: boolean;
  healthStatusText: string;
  wsConnected: boolean;
  feedState: FeedStatus;
  feedFresh: boolean;
  tickRate: number;
  lastTickTime: string | null;
  lastScrapeTime: Date | null;
  age: number | null;
  metrics: Record<string, number>;
  history: Sample[];
  halted: boolean;
  queryError: Error | null;
  healthError: Error | null;
}

export function useConnectionState() {
  const [clock, setClock] = useState(0);
  const [history, setHistory] = useState<Sample[]>([]);
  const [lastTickTime, setLastTickTime] = useState<string | null>(null);
  const [wsConnected, setWsConnected] = useState(false);

  useEffect(() => {
    const id = setInterval(() => setClock(Date.now()), 1000);
    return () => clearInterval(id);
  }, []);

  // 1. Prometheus Metrics Ingestion Query
  const metricsQuery = useQuery({
    queryKey: ['metrics'],
    queryFn: async () => {
      const text = await request('metrics');
      return parseMetrics(text);
    },
    refetchInterval: 3000,
    retry: false,
    refetchOnWindowFocus: false,
  });

  // 2. Health Endpoint Query
  const healthQuery = useQuery({
    queryKey: ['health'],
    queryFn: async () => {
      const text = await request('health');
      if (text.trim() !== 'ok') throw new Error('Health check returned non-ok');
      return true;
    },
    refetchInterval: 5000,
    retry: false,
    refetchOnWindowFocus: false,
  });

  // Track metrics updates, history, and last tick arrival
  useEffect(() => {
    if (metricsQuery.data && !metricsQuery.isError) {
      const rate = metricsQuery.data.luv_execution_tick_rate_hz;
      const risk = metricsQuery.data.luv_execution_risk_check_latency_nanoseconds;
      const timeStr = new Date(metricsQuery.dataUpdatedAt).toLocaleTimeString('en-US', {
        hour12: false,
      });

      setHistory((prev) => [
        ...prev.slice(-119),
        { time: timeStr, rate, risk },
      ]);

      // If tick rate > 0, update lastTickTime to this timestamp
      if (rate && rate > 0) {
        setLastTickTime(timeStr);
      }
    }
  }, [metricsQuery.dataUpdatedAt, metricsQuery.data, metricsQuery.isError]);

  const metricsFresh =
    !!metricsQuery.data &&
    !metricsQuery.isError &&
    (!clock || clock - metricsQuery.dataUpdatedAt < 10000);

  const healthOk =
    !!healthQuery.data &&
    !healthQuery.isError &&
    (!clock || clock - healthQuery.dataUpdatedAt < 15000);

  const isOnline = healthOk && metricsFresh;
  const metrics = metricsFresh ? metricsQuery.data! : {};
  const halted = (metrics.luv_execution_halted ?? 0) === 1;
  const tickRate = metrics.luv_execution_tick_rate_hz ?? 0;

  // Derive honest Feed State
  let feedState: FeedStatus = 'offline';
  if (!isOnline) {
    feedState = 'offline';
  } else if (halted) {
    feedState = 'stalled';
  } else if (tickRate > 0) {
    feedState = 'ingesting';
  } else {
    feedState = 'idle';
  }

  const feedFresh = isOnline && !halted && feedState !== 'stalled';

  const age =
    metricsQuery.dataUpdatedAt && clock
      ? Math.max(0, Math.floor((clock - metricsQuery.dataUpdatedAt) / 1000))
      : null;

  return {
    isOnline,
    healthOk,
    healthStatusText: healthOk ? '200 OK' : healthQuery.isPending ? 'Checking…' : '503 Fail',
    wsConnected,
    setWsConnected,
    feedState,
    feedFresh,
    tickRate,
    lastTickTime,
    lastScrapeTime: metricsQuery.dataUpdatedAt ? new Date(metricsQuery.dataUpdatedAt) : null,
    age,
    metrics,
    history,
    halted,
    queryError: metricsQuery.error,
    healthError: healthQuery.error,
    refetchMetrics: () => metricsQuery.refetch(),
    refetchHealth: () => healthQuery.refetch(),
  };
}
