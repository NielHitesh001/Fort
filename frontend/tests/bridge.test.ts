import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {once} from 'node:events';
import {bridge} from '../server/bridge.mjs';
test('HTTP bridge forwards only exact contracts, retains status and auth on server', async () => {
 const seen:{url?:string;method?:string;auth?:string;body:string}[]=[];
 const upstream=createServer(async(req,res)=>{let body='';for await(const chunk of req) body+=chunk;seen.push({url:req.url,method:req.method,auth:req.headers.authorization,body}); if(req.url==='/metrics'){res.end('luv_execution_halted 0\n');return;} if(req.url==='/healthz'){res.end('ok\n');return;} if(req.method==='POST'){res.writeHead(202,{'Content-Type':'application/json'}).end('{"order_id":7,"status":"accepted"}');return;} res.writeHead(404).end('{"error":"not_found"}');});
 upstream.listen(0,'127.0.0.1');await once(upstream,'listening'); const upstreamPort=(upstream.address() as {port:number}).port;
 const config={port:0,engine:`http://127.0.0.1:${upstreamPort}`,metrics:`http://127.0.0.1:${upstreamPort}`,engineToken:'test-server-only-api',metricsToken:'test-server-only-metrics',ledger:undefined};
 const proxy=createServer((req,res)=>void bridge(req,res,config));proxy.listen(0,'127.0.0.1');await once(proxy,'listening');config.port=(proxy.address() as {port:number}).port;
 const origin=`http://127.0.0.1:${config.port}`;
 try {
  const metrics=await fetch(origin+'/api/corridor/metrics');assert.equal(metrics.status,200);assert.equal(await metrics.text(),'luv_execution_halted 0\n');assert.equal(seen[0].auth,config.metricsToken.replace(/^/,'Bearer '));
  const post=await fetch(origin+'/api/corridor/orders',{method:'POST',headers:{Origin:origin,'Content-Type':'application/json'},body:JSON.stringify({symbol_idx:0,side:'buy',qty:3,price:123456})});assert.equal(post.status,202);assert.equal((await post.json()).status,'accepted');assert.equal(seen[1].auth,'Bearer test-server-only-api');
  const badOrigin=await fetch(origin+'/api/corridor/orders',{method:'POST',headers:{Origin:'https://evil.example','Content-Type':'application/json'},body:'{}'});assert.equal(badOrigin.status,403);assert.equal(seen.length,2);
  const invalid=await fetch(origin+'/api/corridor/orders',{method:'POST',headers:{Origin:origin,'Content-Type':'application/json'},body:'{"symbol_idx":0,"side":"buy","qty":0,"price":1}'});assert.equal(invalid.status,400);assert.equal(seen.length,2);
  const modify=await fetch(origin+'/api/corridor/orders/7',{method:'PATCH',headers:{Origin:origin}});assert.equal(modify.status,404);
  const query=await fetch(origin+'/api/corridor/orders/7');assert.equal(query.status,404);assert.equal((await query.json()).error,'not_found');
  const cancel=await fetch(origin+'/api/corridor/orders/7',{method:'DELETE',headers:{Origin:origin}});assert.equal(cancel.status,404);assert.equal(seen.at(-1)?.method,'DELETE');
  config.engineToken='';const missing=await fetch(origin+'/api/corridor/orders/7');assert.equal(missing.status,503);
  const configResponse=await(await fetch(origin+'/api/corridor/config')).text();assert.ok(!configResponse.includes('test-server-only'));
 }finally{proxy.closeAllConnections();proxy.close();upstream.closeAllConnections();upstream.close();}
});
