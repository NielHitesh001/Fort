'use client';
import {useCallback, useEffect, useRef, useState} from 'react';
import {ArrowRight, RefreshCw, Search, Unplug} from 'lucide-react';
import {Button} from '@/components/ui/button';
import {Dialog, DialogContent, DialogTitle, DialogDescription} from '@/components/ui/dialog';
import {fixed, integer, orderPayload, parseAccepted, parseFill, parseOrder, request, type Fill, type Order} from '@/lib/contracts';
import {useConsole} from '@/lib/store';
export function Execution({enabled, halted}: {enabled: boolean; halted: boolean}) {
  const {orderId, setOrderId} = useConsole();
  const [lookup, setLookup] = useState(orderId);
  const [symbol, setSymbol] = useState('0'); const [side, setSide] = useState('buy');
  const [qty, setQty] = useState(''); const [price, setPrice] = useState('');
  const [review, setReview] = useState<ReturnType<typeof orderPayload>|null>(null);
  const [cancelReview, setCancelReview] = useState(false);
  const [busy, setBusy] = useState(false); const locked = useRef(false);
  const [notice, setNotice] = useState(''); const [error, setError] = useState('');
  const [order, setOrder] = useState<Order|null>(null); const [fills, setFills] = useState<Fill[]>([]);
  const [stream, setStream] = useState('Not subscribed'); const [gap, setGap] = useState(false);
  const [observedAt, setObservedAt] = useState('');
  const [epoch, setEpoch] = useState(0); const currentId = useRef(orderId); currentId.current = orderId;
  const revision = useRef(0);
  useEffect(() => setLookup(orderId), [orderId]);
  const mounted = useRef(true); useEffect(() => {mounted.current = true; return () => {mounted.current = false;};}, []);
  const reconcile = useCallback(async (id: string) => {
    const serial = ++revision.current;
    try {const result = parseOrder(await request(`orders/${id}`)); if (mounted.current && currentId.current === id && serial === revision.current) {setOrder(result); setObservedAt(new Date().toLocaleTimeString('en-GB')); setError('');}}
    catch (e) {if (mounted.current && currentId.current === id && serial === revision.current) setError(`Order query: ${(e as Error).message}. No execution outcome inferred.`);}
  }, []);
  useEffect(() => {
    setOrder(null); setFills([]); setObservedAt(''); setError('');
    if (!orderId || !enabled) {setStream('Not subscribed'); return;}
    setStream('Connecting'); void reconcile(orderId);
    const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/corridor/stream/${orderId}`);
    let alive = true;
    ws.onopen = () => {if (alive) {setStream('Connected'); void reconcile(orderId);}};
    ws.onmessage = event => {
      if (!alive) return;
      try {const fill = parseFill(event.data); if (fill.order_id !== orderId) throw Error('Subscription identity mismatch'); setFills(old => [fill, ...old].slice(0, 200)); void reconcile(orderId);}
      catch {setGap(true); setError('Invalid stream frame. Reconcile before relying on this view.'); ws.close();}
    };
    ws.onclose = () => {if (alive) {setStream('Disconnected'); setGap(true);}};
    ws.onerror = () => {if (alive) {setStream('Disconnected'); setGap(true);}};
    return () => {alive = false; ws.close();};
  }, [orderId, enabled, epoch, reconcile]);
  async function submit() {
    if (!review || locked.current) return;
    locked.current = true; setBusy(true); setError('');
    try {const id = parseAccepted(await request('orders', {method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(review)})); setNotice(`Request queued · order ${id}. “accepted” is an HTTP receipt, not an execution state.`); setOrderId(id); setLookup(id); setGap(true); setReview(null);}
    catch (e) {setError(`Submission outcome uncertain: ${(e as Error).message}. Do not blindly retry; this API has no idempotency key or order-list endpoint.`); setReview(null);}
    finally {locked.current = false; setBusy(false);}
  }
  async function cancel() {
    if (locked.current || !orderId) return; locked.current = true; setBusy(true);
    revision.current++;
    try {const result = parseOrder(await request(`orders/${orderId}`, {method: 'DELETE'})); revision.current++; setOrder(old => old ? {...old, status: result.status} : result); setObservedAt(new Date().toLocaleTimeString('en-GB')); setNotice(`Cancel response: ${result.status}. Local simulator cancellation.`); setError('');}
    catch (e) {setError(`Cancel outcome uncertain: ${(e as Error).message}. Query the order before taking another action.`);}
    finally {locked.current = false; setBusy(false); setCancelReview(false);}
  }
  function findOrder() {try {const id = integer(lookup, 1n); if (id === orderId) void reconcile(id); else {setGap(true); setOrderId(id);}} catch {setError('Enter a positive uint64 order ID.');}}
  return <>
    <div className="workspace-grid execution-grid">
      <section className="panel"><div className="panel-head"><h2>Order ticket</h2><span className="mono muted">LIMIT · DAY</span></div><form className="ticket" onSubmit={e => {e.preventDefault(); try {setReview(orderPayload(symbol, side, qty, price)); setError('');} catch (e) {setError((e as Error).message);}}}>
      <div className="ticket-scope">SIMULATOR ORDER · NO LIVE VENUE</div>
      <label>Registered symbol index<input aria-label="Registered symbol index" inputMode="numeric" value={symbol} onChange={e => setSymbol(e.target.value)}/></label>
      <p className="hint">0–511 · use the engine’s registered index. No exchange ticker mapping is exposed.</p>
      <label>Side<select value={side} onChange={e => setSide(e.target.value)}><option value="buy">Buy</option><option value="sell">Sell</option></select></label>
      <div className="form-pair"><label>Quantity<input inputMode="numeric" value={qty} placeholder="Units" onChange={e => setQty(e.target.value)}/></label><label>Limit price<input inputMode="decimal" value={price} placeholder="0.0000" onChange={e => setPrice(e.target.value)}/></label></div>
      <p className="hint">Price × 10⁴ on the wire. Currency is not identified by this API.</p>
      <Button type="submit" disabled={!enabled || halted || busy}>Review order <ArrowRight/></Button>
      {!enabled && <p className="hint">Configure the server’s API credential to enable order entry.</p>}{halted && <p className="danger-text">Execution halt reported. Submission disabled.</p>}
      <div className="ticket-foot"><span>Modify / market / TIF selection</span><span>Not exposed by HTTP</span></div>
      </form></section>
      <section className="panel"><div className="panel-head"><h2>Order supervision</h2><span className="chip">{stream}</span></div><div className="lookup"><input aria-label="Order ID" value={lookup} onChange={e => setLookup(e.target.value)} placeholder="Enter order ID" onKeyDown={e => {if (e.key === 'Enter') findOrder();}}/><Button variant="outline" onClick={findOrder} disabled={!enabled}><Search/>Query order</Button></div>
      {notice && <div className="notice" role="status">{notice}</div>}{error && <div className="notice" role="alert">{error}</div>}
      {gap && orderId && <div className="notice">Stream history may be incomplete. No replay cursor is available; snapshots reconcile status only.</div>}
      <div className="table-scroll"><table><thead><tr><th>Order ID</th><th>Symbol</th><th>Backend status</th><th>Qty / filled</th><th>Limit price</th></tr></thead><tbody>{order ? <tr><td>{order.order_id}</td><td>{order.symbol_idx}</td><td><span className={`chip ${order.status === 'rejected' ? 'bad' : order.status === 'pending' ? 'warn' : ''}`}>{order.status}</span></td><td>{order.qty} / {order.filled_qty}</td><td>{fixed(order.price)}</td></tr> : <tr><td colSpan={5}><div className="empty-table"><Unplug size={22}/><strong>No order snapshot</strong><span>Query an order ID or submit a simulator order.</span></div></td></tr>}</tbody></table></div>
      <div className="panel-actions"><span className="hint">{observedAt ? `Snapshot observed ${observedAt}; not a continuous status stream.` : 'Statuses are supplied by ExecutionBridge.'}</span><Button variant="outline" disabled={!orderId || !enabled || busy} onClick={() => {setEpoch(e => e + 1); void reconcile(orderId);}}><RefreshCw/>Reconnect & query</Button><Button variant="outline" disabled={!order || !['live','partial','pending'].includes(order.status) || busy} onClick={() => setCancelReview(true)}>Cancel order</Button></div>
      </section>
    </div>
    <section className="panel"><div className="panel-head"><h2>Execution fills</h2><span className="mono muted">PER-ORDER WEBSOCKET · LATEST 200</span></div><div className="table-scroll"><table><thead><tr><th>UTC timestamp (ns)</th><th>Order ID</th><th>Fill quantity</th><th>Fill price</th><th>Event</th></tr></thead><tbody>{fills.length ? fills.map((f,i) => <tr key={`${f.timestamp_ns}-${i}`}><td>{f.timestamp_ns}</td><td>{f.order_id}</td><td>{f.filled_qty}</td><td>{fixed(f.fill_price)}</td><td>fill</td></tr>) : <tr><td colSpan={5}><div className="empty-table small">No fill notifications received in this subscription.</div></td></tr>}</tbody></table></div></section>
    <Dialog open={!!review} onOpenChange={open => {if (!open && !busy) setReview(null);}}><DialogContent><DialogTitle>Review simulator order</DialogTitle><DialogDescription>No live capital or venue connection. A queue receipt does not establish execution.</DialogDescription>{review && <dl className="review"><dt>Symbol index</dt><dd>{review.symbol_idx}</dd><dt>Side</dt><dd>{review.side}</dd><dt>Quantity</dt><dd>{review.qty}</dd><dt>Limit price</dt><dd>{fixed(String(review.price))}</dd></dl>}<Button onClick={submit} disabled={busy || halted}>{busy ? 'Submitting…' : 'Submit simulator order'}</Button></DialogContent></Dialog>
    <Dialog open={cancelReview} onOpenChange={setCancelReview}><DialogContent><DialogTitle>Cancel order {orderId}</DialogTitle><DialogDescription>This requests local simulator cancellation. A concurrent fill may win the race; inspect the returned outcome.</DialogDescription><Button onClick={cancel} disabled={busy}>{busy ? 'Sending…' : 'Confirm cancellation'}</Button></DialogContent></Dialog>
  </>;
}
