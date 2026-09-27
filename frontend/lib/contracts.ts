import JSONbig from 'json-bigint';

const json = JSONbig({
  storeAsString: true,
  strict: true,
  protoAction: 'error',
  constructorAction: 'error',
});

export const statuses = [
  'pending',
  'live',
  'partial',
  'cancelled',
  'done',
  'rejected',
  'not_found',
] as const;

export type OrderStatus = typeof statuses[number];

export type Order = {
  order_id: string;
  status: OrderStatus;
  symbol_idx: number;
  qty: string;
  filled_qty: string;
  price: string;
  side?: 'buy' | 'sell';
  reject_reason?: string;
  submitted_at?: string;
  updated_at?: string;
};

export type Fill = {
  event: 'fill';
  order_id: string;
  filled_qty: string;
  fill_price: string;
  timestamp_ns: string;
};

export type Position = {
  symbol_idx: number;
  net_position: string;
  gross_exposure: string;
};

export type PriceLevelData = {
  price: string;
  qty: string;
  order_count: number;
  depth_pct: number;
};

export type OrderBookState = {
  symbol: string;
  symbol_idx: number;
  bids: PriceLevelData[];
  asks: PriceLevelData[];
  spread: string;
  spread_bps: string;
  last_update: string;
  feed_stalled: boolean;
};

export const CircuitBreakerStates = {
  kClosed: 'kClosed (Normal Operation)',
  kHalfOpen: 'kHalfOpen (Probe Mode)',
  kOpen: 'kOpen (Halted / Tripped)',
} as const;

export type CircuitBreakerState = keyof typeof CircuitBreakerStates;

export const MemoryPressureTiers = {
  kNormal: { name: 'Normal', threshold: '< 70%', status: 'ok' },
  kWarning: { name: 'Warning', threshold: '≥ 70%', status: 'warn' },
  kShedLoad: { name: 'Shed Load (Cancels Only)', threshold: '≥ 85%', status: 'critical' },
  kEmergencyHalt: { name: 'Emergency Halt (Hard OOM Protect)', threshold: '≥ 95%', status: 'critical' },
} as const;

export const RejectMasks: Record<number, string> = {
  1: 'RejectQty: Quantity exceeds limit or zero',
  2: 'RejectPosition: Position or gross exposure limit breached',
  4: 'RejectStaleAlpha: Alpha signal timestamp exceeds 250 us threshold',
  8: 'RejectHalted: Trading halted for symbol / kill-switch active',
  16: 'RejectFlatSignal: Signal direction flat / zero confidence',
  32: 'RejectOrderCapacity: Symbol active order capacity (64 max) reached',
  64: 'RejectInvalidSymbol: Symbol index out of valid range (0..511)',
  128: 'RejectPrice: Price violates collar or boundary limits',
};

export function parseRejectMask(mask: number): string {
  if (!mask) return 'Unknown rejection';
  const reasons: string[] = [];
  for (const [bit, label] of Object.entries(RejectMasks)) {
    if (mask & Number(bit)) {
      reasons.push(label);
    }
  }
  return reasons.join('; ') || `Reject code 0x${mask.toString(16)}`;
}

export function parseJSON(text: string): unknown {
  return json.parse(text);
}

function record(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new Error('Invalid response object');
  }
  return value as Record<string, unknown>;
}

export function integer(
  value: unknown,
  min = 0n,
  max = 18446744073709551615n
): string {
  if (
    (typeof value !== 'number' && typeof value !== 'string') ||
    (typeof value === 'number' && !Number.isSafeInteger(value)) ||
    !/^-?\d+$/.test(String(value))
  ) {
    throw new Error('Invalid integer');
  }
  const n = BigInt(value);
  if (n < min || n > max) {
    throw new Error(`Integer out of range [${min}..${max}]`);
  }
  return String(n);
}

export function parseOrder(text: string): Order {
  const v = record(parseJSON(text));
  if (!statuses.includes(v.status as OrderStatus)) {
    throw new Error(`Unknown backend order status: ${String(v.status)}`);
  }
  const order: Order = {
    order_id: integer(v.order_id, 1n),
    status: v.status as OrderStatus,
    symbol_idx: Number(integer(v.symbol_idx, 0n, 511n)),
    qty: integer(v.qty),
    filled_qty: integer(v.filled_qty),
    price: integer(v.price, 0n, 4294967295n),
  };
  if (BigInt(order.filled_qty) > BigInt(order.qty)) {
    throw new Error('Invalid fill quantity: exceeds order quantity');
  }
  return order;
}

export function parseFill(text: string): Fill {
  const v = record(parseJSON(text));
  if (v.event !== 'fill') {
    throw new Error('Unsupported stream event type');
  }
  return {
    event: 'fill',
    order_id: integer(v.order_id, 1n),
    filled_qty: integer(v.filled_qty, 1n),
    fill_price: integer(v.fill_price, 1n, 4294967295n),
    timestamp_ns: integer(v.timestamp_ns, 1n),
  };
}

export function parseAccepted(text: string): string {
  const v = record(parseJSON(text));
  if (v.status !== 'accepted' && !v.order_id) {
    throw new Error('Unknown submission outcome');
  }
  return integer(v.order_id, 1n);
}

export function parsePositions(text: string): Position[] {
  const v = record(parseJSON(text));
  if (!Array.isArray(v.positions)) return [];
  return v.positions.map((p) => {
    const item = record(p);
    return {
      symbol_idx: Number(integer(item.symbol_idx, 0n, 511n)),
      net_position: integer(item.net_position, -9223372036854775808n, 9223372036854775807n),
      gross_exposure: integer(item.gross_exposure, 0n, 9223372036854775807n),
    };
  });
}

export function parseMetrics(text: string): Record<string, number> {
  const metrics: Record<string, number> = {};
  for (const line of text.split('\n')) {
    if (!line || line.startsWith('#')) continue;
    const match = line.match(
      /^(luv_[a-zA-Z0-9_]+)\s+([-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)\s*$/
    );
    if (match && Number.isFinite(Number(match[2]))) {
      metrics[match[1]] = Number(match[2]);
    }
  }
  if (!Object.keys(metrics).length) {
    throw new Error('No supported Fort metrics in response');
  }
  return metrics;
}

export function fixed(value: string | number, decimals = 4): string {
  if (value === undefined || value === null || value === '') return '—';
  const n = BigInt(value);
  const a = n < 0n ? -n : n;
  const divisor = 10000n;
  const whole = a / divisor;
  const frac = a % divisor;
  const fracStr = String(frac).padStart(4, '0').slice(0, decimals);
  return `${n < 0n ? '-' : ''}${whole.toLocaleString('en-US')}.${fracStr}`;
}

export function orderPayload(
  symbol: string,
  side: string,
  qty: string,
  price: string,
  type = 'limit'
) {
  if (!['buy', 'sell'].includes(side)) {
    throw new Error('Choose side: Buy or Sell');
  }
  if (type === 'market') {
    return {
      symbol_idx: Number(integer(symbol, 0n, 511n)),
      side,
      qty: Number(integer(qty, 1n, 1000n)), // Validated against max_order_qty=1000
      price: side === 'buy' ? 4294967295 : 1, // Cap for market orders
    };
  }
  if (!/^\d+(?:\.\d{1,4})?$/.test(price)) {
    throw new Error('Price requires a positive decimal value with up to 4 decimal places');
  }
  const [whole, fraction = ''] = price.split('.');
  const units = BigInt(whole) * 10000n + BigInt(fraction.padEnd(4, '0'));
  return {
    symbol_idx: Number(integer(symbol, 0n, 511n)),
    side,
    qty: Number(integer(qty, 1n, 1000n)),
    price: Number(integer(String(units), 1n, 4294967295n)),
  };
}

export async function request(path: string, options?: RequestInit): Promise<string> {
  const response = await fetch('/api/corridor/' + path, {
    cache: 'no-store',
    signal: AbortSignal.timeout(6000),
    ...options,
  });
  const body = await response.text();
  if (!response.ok) {
    let reason = `HTTP ${response.status}`;
    try {
      const obj = record(parseJSON(body));
      if (typeof obj.error === 'string') reason += ` · ${obj.error}`;
    } catch {
      // Plain text fallback
    }
    throw new Error(reason);
  }
  return body;
}

export function exportToCSV(filename: string, headers: string[], rows: (string | number)[][]) {
  const csvContent = [
    headers.join(','),
    ...rows.map((row) =>
      row
        .map((val) => {
          const str = String(val ?? '');
          return str.includes(',') || str.includes('"') || str.includes('\n')
            ? `"${str.replace(/"/g, '""')}"`
            : str;
        })
        .join(',')
    ),
  ].join('\n');

  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.setAttribute('href', url);
  link.setAttribute('download', filename);
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
}
