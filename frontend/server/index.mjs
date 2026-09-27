import { createServer } from 'node:http';
import next from 'next';
import WebSocket, { WebSocketServer } from 'ws';
import { bridge, endpoint, trustedRequest } from './bridge.mjs';
const port = Number(process.env.PORT || 3000);
const config = {port, engine: endpoint(process.env.CORRIDOR_ENGINE_URL || 'http://127.0.0.1:8080'), metrics: endpoint(process.env.CORRIDOR_METRICS_URL || 'http://127.0.0.1:9090'), engineToken: process.env.CORRIDOR_API_TOKEN, metricsToken: process.env.CORRIDOR_METRICS_TOKEN || process.env.CORRIDOR_API_TOKEN, ledger: process.env.CORRIDOR_LEDGER_PATH};
const app = next({dev: process.env.NODE_ENV !== 'production', hostname: '127.0.0.1', port});
await app.prepare();
const handler = app.getRequestHandler();
const server = createServer((req, res) => {
  if (!trustedRequest(req, port)) { res.writeHead(403).end('Invalid origin'); return; }
  if (req.url?.startsWith('/api/corridor/')) void bridge(req, res, config);
  else void handler(req, res);
});
server.requestTimeout = 10000;
const sockets = new WebSocketServer({noServer: true, maxPayload: 8192, perMessageDeflate: false});
let pending = 0;
server.on('upgrade', (req, socket, head) => {
  if (!trustedRequest(req, port, true)) return socket.destroy();
  if (req.url?.startsWith('/_next/')) return; // Next attaches its HMR upgrade handler after the first page request.
  const match = req.url?.match(/^\/api\/corridor\/stream\/([1-9][0-9]{0,19})$/);
  if (!match || BigInt(match[1]) > 18446744073709551615n || !config.engineToken || sockets.clients.size + pending >= 64) return socket.destroy();
  pending++;
  const url = config.engine.replace(/^http/, 'ws') + `/api/v1/stream/${match[1]}`;
  const upstream = new WebSocket(url, {headers: {Authorization: `Bearer ${config.engineToken}`}, handshakeTimeout: 4000, maxPayload: 8192, perMessageDeflate: false});
  let client; let released = false;
  const release = () => { if (!released) { pending--; released = true; } };
  socket.once('close', () => { release(); upstream.terminate(); });
  upstream.on('open', () => {
    release();
    if (socket.destroyed) return upstream.terminate();
    sockets.handleUpgrade(req, socket, head, ws => {
      client = ws; sockets.emit('connection', ws, req);
      ws.on('close', () => upstream.terminate());
      ws.on('error', () => upstream.terminate());
      ws.on('message', () => ws.close(1008, 'Read-only fill subscription'));
    });
  });
  upstream.on('message', (data, binary) => {
    if (!client || client.readyState !== WebSocket.OPEN) return;
    if (binary || client.bufferedAmount > 65536) { client.close(1013, 'Stream continuity lost'); upstream.terminate(); return; }
    client.send(data.toString(), {binary: false});
  });
  upstream.on('error', () => { release(); if (client) client.close(1011, 'Upstream unavailable'); else socket.destroy(); });
  upstream.on('close', () => { release(); if (client) client.close(1012, 'Stream ended; reconcile order'); else socket.destroy(); });
});
server.listen(port, '127.0.0.1', () => console.log(`Corridor: http://127.0.0.1:${port} (research console; loopback only)`));
async function shutdown() { for (const client of sockets.clients) client.terminate(); server.close(); await app.close(); process.exit(0); }
process.on('SIGTERM', shutdown); process.on('SIGINT', shutdown);
