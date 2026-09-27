import {test} from 'node:test';
import assert from 'node:assert/strict';
import {integer, parseAccepted, parseFill, parseOrder, parseMetrics, orderPayload, fixed} from '../lib/contracts';
import {decodeLedger} from '../server/ledger.mjs';
import {routeFor, trustedRequest, endpoint} from '../server/bridge.mjs';
test('uint64 IDs and epoch nanoseconds survive JavaScript parsing', () => {
 const fill = parseFill('{"event":"fill","order_id":18446744073709551615,"filled_qty":3,"fill_price":123456,"timestamp_ns":1790520000123456789}');
 assert.equal(fill.order_id, '18446744073709551615'); assert.equal(fill.timestamp_ns,'1790520000123456789');
 assert.throws(()=>integer(9007199254740992)); assert.throws(()=>integer('18446744073709551616')); assert.throws(()=>parseFill('{"event":"book"}'));
});
test('exact decimal scaling and engine numeric bounds', () => {
 assert.deepEqual(orderPayload('511','sell','1000','123.4567'),{symbol_idx:511,side:'sell',qty:1000,price:1234567});
 assert.equal(fixed('1234567'),'123.4567'); assert.equal(fixed('-12'),'-0.0012');
 for(const args of [['512','buy','1','1'],['0','buy','1.2','1'],['0','buy','1','0'],['0','buy','1','1.00001'],['0','buy','1','429496.7296'],['0','short','1','1']]) assert.throws(()=>orderPayload(...args as [string,string,string,string]));
});
test('receipt is not an order state, and unknown states fail closed', () => {
 assert.equal(parseAccepted('{"order_id":42,"status":"accepted"}'),'42');
 for(const status of ['pending','live','partial','cancelled','done','rejected','not_found']) assert.equal(parseOrder(JSON.stringify({order_id:42,status,symbol_idx:0,qty:10,filled_qty:4,price:1})).status,status);
 for(const status of ['accepted','filled','working']) assert.throws(()=>parseOrder(JSON.stringify({order_id:42,status,symbol_idx:0,qty:10,filled_qty:4,price:1})));
 assert.throws(()=>parseOrder('{"order_id":1,"status":"partial","symbol_idx":0,"qty":1,"filled_qty":2,"price":1}'));
});
test('metrics preserve zero but do not invent absent percentiles', () => {
 const m = parseMetrics('# TYPE luv_execution_halted gauge\nluv_execution_halted 0\nluv_execution_tick_rate_hz 42000\nluv_bad NaN\n');
 assert.equal(m.luv_execution_halted,0); assert.equal(m.luv_execution_tick_rate_hz,42000); assert.equal(m.p99,undefined); assert.equal(m.luv_bad,undefined); assert.throws(()=>parseMetrics('<html>unauthorized</html>'));
});
test('proxy is allowlisted and guarded against cross-origin access', () => {
 assert.equal(routeFor('GET','/api/corridor/metrics')?.path,'/metrics');
 assert.equal(routeFor('POST','/api/corridor/orders')?.path,'/api/v1/orders');
 assert.equal(routeFor('PATCH','/api/corridor/orders/42'),null);
 assert.equal(routeFor('GET','/api/corridor/orders/42?url=http://evil'),null);
 assert.equal(routeFor('GET','/api/corridor/orders/18446744073709551616'),null);
 assert.equal(trustedRequest({headers:{host:'127.0.0.1:3000',origin:'http://127.0.0.1:3000'}},3000,true),true);
 assert.equal(trustedRequest({headers:{host:'127.0.0.1:3000',origin:'https://evil.example'}},3000,true),false);
 assert.equal(trustedRequest({headers:{host:'evil.example:3000'}},3000),false);
 assert.equal(trustedRequest({headers:{host:'localhost:3000'}},3000,true),false);
 assert.throws(()=>endpoint('http://localhost:8080/path')); assert.throws(()=>endpoint('file:///tmp/data'));
});
function record(seq:number) {const b=Buffer.alloc(64);b.writeUInt32LE(0x31564352);b[4]=2;b[5]=1;b.writeBigUInt64LE(BigInt(seq),8);b.writeBigUInt64LE(9007199254740993n,16);b.writeBigInt64LE(10n,24);b.writeBigInt64LE(1234567n,32);b.writeBigUInt64LE(1790520000123456789n,48);let hash=2166136261;for(let i=0;i<56;i++)hash=Math.imul(hash^b[i],16777619)>>>0;b.writeUInt32LE(hash,56);return b;}
test('recovery reader verifies C++ v2 records, sequence, checksum and truncation', () => {
 const parsed = decodeLedger(Buffer.concat([record(0),record(1)])); assert.equal(parsed.total,2);assert.equal(parsed.rows[0].order_id,'9007199254740993');assert.equal(parsed.rows[0].type,'kAdd');
 assert.throws(()=>decodeLedger(record(0).subarray(0,63)));assert.throws(()=>decodeLedger(record(1)));
 const corrupt=record(0);corrupt[30]^=1;assert.throws(()=>decodeLedger(corrupt));
 const wrongVersion=record(0);wrongVersion[4]=1;assert.throws(()=>decodeLedger(wrongVersion));
 assert.equal(decodeLedger(Buffer.alloc(0)).total,0);
});
