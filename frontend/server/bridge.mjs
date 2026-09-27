import { readLedger } from './ledger.mjs';
export function trustedRequest(req, port, requireOrigin = false) {
  const hosts = [`127.0.0.1:${port}`, `localhost:${port}`];
  const origin = req.headers.origin;
  return hosts.includes(req.headers.host) && (!origin ? !requireOrigin : origin === `http://${req.headers.host}`) && !['cross-site', 'same-site'].includes(req.headers['sec-fetch-site']);
}
export function endpoint(base) {
  const url = new URL(base);
  if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password || url.pathname !== '/' || url.search || url.hash) throw new Error('Configure an HTTP(S) origin without a path or credentials');
  return url.origin;
}
export function routeFor(method, path) {
  if (method === 'GET' && path === '/api/corridor/metrics') return {target: 'metrics', path: '/metrics'};
  if (method === 'GET' && path === '/api/corridor/health') return {target: 'metrics', path: '/healthz'};
  if (method === 'GET' && path === '/api/corridor/positions') return {target: 'engine', path: '/api/v1/positions'};
  if (method === 'POST' && path === '/api/corridor/orders') return {target: 'engine', path: '/api/v1/orders'};
  const match = path.match(/^\/api\/corridor\/orders\/([1-9][0-9]{0,19})$/);
  if (match && BigInt(match[1]) <= 18446744073709551615n && ['GET','DELETE'].includes(method)) return {target: 'engine', path: `/api/v1/orders/${match[1]}`};
  return null;
}
function reply(res, status, body, contentType = 'application/json') {
  res.writeHead(status, {'Content-Type': contentType, 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff'});
  res.end(typeof body === 'string' ? body : JSON.stringify(body));
}
export async function bridge(req, res, config) {
  if (!trustedRequest(req, config.port, req.method !== 'GET')) return reply(res, 403, {error: 'origin_rejected'});
  const path = req.url;
  if (req.method === 'GET' && path === '/api/corridor/config') return reply(res, 200, {orders: !!config.engineToken, metrics: !!config.metricsToken, ledger: !!config.ledger});
  if (req.method === 'GET' && path === '/api/corridor/ledger') {
    if (!config.ledger) return reply(res, 503, {error: 'ledger_not_configured'});
    try { return reply(res, 200, await readLedger(config.ledger)); }
    catch { return reply(res, 422, {error: 'ledger_integrity_or_read_failure'}); }
  }
  const route = routeFor(req.method, path);
  if (!route) return reply(res, 404, {error: 'unsupported_endpoint'});
  const token = route.target === 'engine' ? config.engineToken : config.metricsToken;
  if (!token && route.path !== '/healthz') return reply(res, 503, {error: 'credential_not_configured'});
  try {
    let body;
    if (req.method === 'POST') {
      if (!req.headers['content-type']?.startsWith('application/json')) return reply(res, 415, {error: 'json_required'});
      const chunks = []; let size = 0;
      for await (const chunk of req) { size += chunk.length; if (size > 1024) return reply(res, 413, {error: 'request_too_large'}); chunks.push(chunk); }
      body = Buffer.concat(chunks).toString('utf8');
      const order = JSON.parse(body);
      // Bound the public contract; never accept arbitrary upstream URLs or keys.
      if (Object.keys(order).sort().join(',') !== 'price,qty,side,symbol_idx' || !Number.isInteger(order.symbol_idx) || order.symbol_idx < 0 || order.symbol_idx >= 512 || !['buy','sell'].includes(order.side) || !Number.isInteger(order.qty) || order.qty < 1 || order.qty > 1e9 || !Number.isInteger(order.price) || order.price < 1 || order.price > 0xffffffff) return reply(res, 400, {error: 'invalid_order'});
    }
    const response = await fetch(config[route.target] + route.path, {method: req.method, headers: {Authorization: `Bearer ${token || ''}`, ...(body ? {'Content-Type': 'application/json'} : {})}, body, redirect: 'error', signal: AbortSignal.timeout(4000)});
    // Bound reads even when the upstream supplies no Content-Length.
    const reader = response.body?.getReader(); const chunks = []; let size = 0;
    if (reader) { while (true) { const {done, value} = await reader.read(); if (done) break; size += value.length; if (size > 128 * 1024) { await reader.cancel(); throw new Error('oversized_response'); } chunks.push(Buffer.from(value)); } }
    return reply(res, response.status, Buffer.concat(chunks).toString('utf8'), route.path === '/metrics' || route.path === '/healthz' ? 'text/plain' : 'application/json');
  } catch (error) {
    return reply(res, error instanceof SyntaxError ? 400 : 502, {error: error instanceof SyntaxError ? 'invalid_json' : 'upstream_unavailable', ...(req.method === 'GET' ? {} : {outcome: 'unknown_do_not_automatically_retry'})});
  }
}
