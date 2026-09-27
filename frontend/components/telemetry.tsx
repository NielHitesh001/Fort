'use client';
import {useEffect, useState} from 'react';
import {useQuery} from '@tanstack/react-query';
import {ResponsiveContainer, CartesianGrid, XAxis, YAxis, Tooltip, Line, LineChart} from 'recharts';
import {parseMetrics, request} from '@/lib/contracts';
export type Sample = {time: string; rate?: number; risk?: number};
export function useTelemetry() {
  const query = useQuery({queryKey: ['metrics'], queryFn: async () => parseMetrics(await request('metrics')), refetchInterval: 3000, retry: false});
  const health = useQuery({queryKey: ['health'], queryFn: async () => {const text = await request('health'); if (text.trim() !== 'ok') throw Error('Unexpected health response'); return true;}, refetchInterval: 5000, retry: false});
  const [history, setHistory] = useState<Sample[]>([]);
  const [clock, setClock] = useState(0);
  useEffect(() => {const id = setInterval(() => setClock(Date.now()), 1000); return () => clearInterval(id);}, []);
  useEffect(() => {if (query.data && !query.isError) setHistory(old => [...old.slice(-119), {time: new Date(query.dataUpdatedAt).toLocaleTimeString('en-GB'), rate: query.data.luv_execution_tick_rate_hz, risk: query.data.luv_execution_risk_check_latency_nanoseconds}]);}, [query.dataUpdatedAt, query.data, query.isError]);
  const fresh = !!query.data && !query.isError && (!clock || clock - query.dataUpdatedAt < 10000);
  const healthFresh = !!health.data && !health.isError && (!clock || clock - health.dataUpdatedAt < 15000);
  return {query, health, healthFresh, history, fresh, metrics: fresh ? query.data! : {}, age: query.dataUpdatedAt && clock ? Math.max(0, Math.floor((clock - query.dataUpdatedAt) / 1000)) : null};
}
export function HistoryChart({history, field, unit}: {history: Sample[]; field: 'rate'|'risk'; unit: string}) {
  if (!history.some(s => s[field] !== undefined)) return <div className="chart-empty"><div className="chart-grid"/><span>No observations received</span><small>Only measurements returned by the engine appear here.</small></div>;
  return <div className="history-chart" role="img" aria-label={`${field === 'rate' ? 'Tick rate' : 'Last risk check latency'} observations in ${unit}`}><ResponsiveContainer width="100%" height="100%"><LineChart data={history} margin={{top: 15, right: 18, left: 0, bottom: 0}}><CartesianGrid stroke="#202b39" strokeDasharray="3 6" vertical={false}/><XAxis dataKey="time" stroke="#8190a3" tick={{fontSize: 10}} minTickGap={65}/><YAxis stroke="#8190a3" tick={{fontSize: 10}} width={68}/><Tooltip contentStyle={{background: '#111b28', border: '1px solid #344152', fontSize: 11}} formatter={value => [`${value} ${unit}`, field]}/><Line type="linear" dataKey={field} stroke="#a0aec2" strokeWidth={1.5} dot={false} isAnimationActive={false} connectNulls={false}/></LineChart></ResponsiveContainer></div>;
}
