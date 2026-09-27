import { open } from 'node:fs/promises';
const types = ['', 'kAdd', 'kFill', 'kCancel', 'kAck', 'kNew', 'kReplace', 'kTerminalFill'];
export function decodeLedger(buffer) {
  if (buffer.length > 4 * 1024 * 1024 || buffer.length % 64) throw new Error('Invalid ledger size or partial record');
  const rows = [];
  for (let offset = 0; offset < buffer.length; offset += 64) {
    const b = buffer.subarray(offset, offset + 64);
    let hash = 2166136261;
    for (let i = 0; i < 56; i++) hash = Math.imul(hash ^ b[i], 16777619) >>> 0;
    if (b.readUInt32LE(0) !== 0x31564352 || b[4] !== 2 || b.readBigUInt64LE(8) !== BigInt(offset / 64) || b.readUInt32LE(56) !== hash || !types[b[5]] || b[40] > 1 || b.readBigUInt64LE(16) === 0n || b.readBigInt64LE(24) <= 0n) throw new Error(`Invalid ledger record at sequence ${offset / 64}`);
    rows.push({sequence: String(offset / 64), type: types[b[5]], order_id: String(b.readBigUInt64LE(16)), quantity: String(b.readBigInt64LE(24)), price: String(b.readBigInt64LE(32)), side: b[40], timestamp_ns: String(b.readBigUInt64LE(48))});
  }
  return {total: rows.length, rows: rows.slice(-500).reverse(), validation: 'Record checksums and sequence verified; no retention or semantic replay guarantee'};
}
export async function readLedger(path) {
  const file = await open(path, 'r');
  try {
    const stat = await file.stat();
    if (!stat.isFile() || stat.size > 4 * 1024 * 1024) throw new Error('Ledger must be a regular file under 4 MiB');
    const buffer = Buffer.alloc(stat.size);
    const { bytesRead } = await file.read(buffer, 0, buffer.length, 0);
    const after = await file.stat();
    if (bytesRead !== stat.size || after.size !== stat.size || after.mtimeMs !== stat.mtimeMs) throw new Error('Ledger changed during read; configure a frozen copy');
    return decodeLedger(buffer);
  } finally { await file.close(); }
}
