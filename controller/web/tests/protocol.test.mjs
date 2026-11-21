// Node tests for the phone's protocol.js:
//  * SHA-256 / HMAC give the same answers as the standard vectors (and the C++ side)
//  * end to end against the real cricket_controller_host over real sockets:
//    pairing, a full two-phone match, automatic reconnection.
// Run: node --test controller/web/tests/   (needs build/cricket_controller_host)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const P = require('../protocol.js');
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../..');
const hostBin = process.env.CU_HOST_BIN || path.join(root, 'build/cricket_controller_host');

const hex = (s) => P.toHex(P.sha256(s));

test('sha256 standard vectors', () => {
  assert.equal(hex(''), 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
  assert.equal(hex('abc'), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  assert.equal(hex('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'),
    '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1');
  assert.equal(hex('a'.repeat(1000000)), 'cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0');
  assert.equal(hex('क्रिकेट 🏏'), P.toHex(P.sha256(new TextEncoder().encode('क्रिकेट 🏏'))));
});

test('hmac RFC 4231 vectors (same as the C++ tests)', () => {
  assert.equal(P.hmacHex(new Uint8Array(20).fill(0x0b), 'Hi There'),
    'b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7');
  assert.equal(P.hmacHex('Jefe', 'what do ya want for nothing?'),
    '5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843');
  assert.equal(P.hmacHex(new Uint8Array(131).fill(0xaa), 'Test Using Larger Than Block-Size Key - Hash Key First'),
    '60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54');
  assert.ok(P.sameHex('ab12', 'ab12'));
  assert.ok(!P.sameHex('ab12', 'ab13'));
});

test('pairing link parsing', () => {
  const secret = 'A'.repeat(42) + 'g';  // 43 chars -> 32 bytes
  const p = P.parsePairing('#p=Abcdefghijkl.' + secret);
  assert.ok(p);
  assert.equal(p.pairId, 'Abcdefghijkl');
  assert.equal(p.secret.length, 32);
  assert.equal(P.parsePairing('#p=short.' + secret), null);
  assert.equal(P.parsePairing('#p=Abcdefghijkl.not-long-enough'), null);
  assert.equal(P.parsePairing(''), null);
  assert.equal(P.parsePairing('#p=Abcdefghijkl.' + secret + '&evil=1'), null);
});

// ---------------------------------------------------------------- end to end

function startHost(args) {
  const proc = spawn(hostBin, ['--port', '0', '--bind', '127.0.0.1', '--host-ip', '127.0.0.1', '--print-links', '--quiet', ...args]);
  const links = [];
  let waiters = [];
  let buf = '';
  proc.stdout.on('data', (d) => {
    buf += d;
    let i;
    while ((i = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, i);
      buf = buf.slice(i + 1);
      if (line.startsWith('LINK ')) {
        links.push(JSON.parse(line.slice(5)));
        waiters.forEach((w) => w());
        waiters = [];
      }
    }
  });
  const linkFor = async (role, side) => {
    for (;;) {
      const l = [...links].reverse().find((x) => x.role === role && x.side === side);
      if (l) { links.splice(links.indexOf(l), 1); return l.link; }
      await new Promise((r) => waiters.push(r));
    }
  };
  return { proc, linkFor };
}

function memoryStorage() {
  const m = new Map();
  return { getItem: (k) => (m.has(k) ? m.get(k) : null), setItem: (k, v) => m.set(k, String(v)), removeItem: (k) => m.delete(k) };
}

// A Node "phone" running the same client code as the real phone page.
function makePhone(link, name) {
  const url = new URL(link);
  const ph = { state: null, statuses: [], acks: 0, rejected: [], ball: null, events: [] };
  ph.client = new P.ControllerClient({
    url: 'ws://' + url.host + '/ws',
    storage: memoryStorage(),
    WebSocket,
    name,
    onEvent: (type, data) => {
      ph.events.push(type);
      if (type === 'status') ph.statuses.push(data.status);
      if (type === 'state') ph.state = data;
      if (type === 'ball') ph.ball = data;
      if (type === 'ack') { ph.acks++; if (!data.ok) ph.rejected.push(data.err); }
    },
  });
  ph.client.start(P.parsePairing(url.hash));
  ph.has = (c) => !!(ph.state && ph.state.controls.includes(c));
  return ph;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(cond, ms = 10000, what = 'condition') {
  const end = Date.now() + ms;
  while (Date.now() < end) { if (cond()) return; await sleep(20); }
  throw new Error('timed out waiting for ' + what);
}

// Plays like a person: one decision per state the TV sends.
function autoPlay(ph, rnd) {
  let last = '';
  return () => {
    const s = ph.state;
    if (!s || ph.client.status !== 'connected') return;
    const sig = JSON.stringify([s.controls, s.options, s.score, s.waiting, s.stage, !!ph.ball]);
    if (sig === last) return;
    last = sig;
    const o = s.options || {};
    if (ph.has('toss-call')) return ph.client.act({ do: 'toss-call', face: rnd() < 0.5 ? 'heads' : 'tails' });
    if (ph.has('toss-decide')) return ph.client.act({ do: 'toss-decide', choice: 'bat' });
    if (ph.has('openers')) return ph.client.act({ do: 'openers', striker: o.batters[0].id, nonStriker: o.batters[1].id });
    if (ph.has('batter')) return ph.client.act({ do: 'batter', player: o.batters[0].id });
    if (ph.has('bowler')) return ph.client.act({ do: 'bowler', player: o.bowlers[0].id });
    if (ph.has('review')) return ph.client.act({ do: rnd() < 0.5 ? 'review' : 'accept' });
    if (ph.has('ready')) return ph.client.act({ do: 'ready' });
    if (ph.has('bowl')) return ph.client.act({ do: 'bowl', variation: s.bowler.variations[0], line: 0, length: rnd(), effort: 70 });
    if (ph.has('bat') && ph.ball) {
      ph.ball = null;
      return ph.client.act({ do: 'bat', shot: ['drive', 'defend', 'pull', 'loft'][Math.floor(rnd() * 4)], dir: 0, timing: Math.round(rnd() * 200 - 100) });
    }
  };
}

test('end to end: two phones play India vs Pakistan through the real host', { timeout: 120000 }, async (t) => {
  const host = startHost(['--humans', '2', '--dev-format', 'overs=2', '--seed', '42']);
  t.after(() => host.proc.kill());
  const india = makePhone(await host.linkFor('captain', 0), 'India phone');
  const pakistan = makePhone(await host.linkFor('captain', 1), 'Pakistan phone');
  await until(() => india.client.status === 'connected' && pakistan.client.status === 'connected', 10000, 'pairing');
  assert.equal(india.client.side, 0);
  assert.equal(pakistan.client.side, 1);
  let seed = 7;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648);
  const a = autoPlay(india, rnd), b = autoPlay(pakistan, rnd);
  const end = Date.now() + 100000;
  while (Date.now() < end && !(india.state && india.state.result)) {
    a(); b();
    await sleep(15);
  }
  assert.ok(india.state.result, 'match finished');
  assert.equal(pakistan.state.result, india.state.result);
  assert.deepEqual(india.rejected, []);
  assert.deepEqual(pakistan.rejected, []);
  assert.ok(india.acks > 10 && pakistan.acks > 10);
});

test('end to end: phone reconnects by itself and keeps its place', { timeout: 60000 }, async (t) => {
  const host = startHost(['--humans', '1', '--dev-format', 'overs=5', '--seed', '9']);
  t.after(() => host.proc.kill());
  const phone = makePhone(await host.linkFor('captain', 0), 'Pixel');
  await until(() => phone.client.status === 'connected', 10000, 'pairing');
  const dev = phone.client.dev;
  // The AI (side 1) calls the toss; wait until the phone has something to do.
  await until(() => phone.state && phone.state.controls.length > 0, 10000, 'first control');
  // Wi-Fi blip: the socket drops.
  phone.client.ws.close();
  await until(() => phone.statuses.includes('reconnecting'), 5000, 'reconnecting status');
  await until(() => phone.client.status === 'connected', 15000, 'automatic reconnection');
  assert.equal(phone.client.dev, dev, 'same paired device, no new QR scan');
  // A full state arrives again and play continues.
  const before = phone.acks;
  const play = autoPlay(phone, Math.random);
  await until(() => { play(); return phone.acks > before + 3; }, 20000, 'actions after reconnecting');
  assert.deepEqual(phone.rejected, []);
});

test('end to end: a used or wrong QR code is refused', { timeout: 30000 }, async (t) => {
  const host = startHost(['--humans', '1', '--dev-format', 'overs=1', '--seed', '3']);
  t.after(() => host.proc.kill());
  const link = await host.linkFor('captain', 0);
  const first = makePhone(link, 'first');
  await until(() => first.client.status === 'connected', 10000, 'pairing');
  const copy = makePhone(link, 'copycat');  // someone photographed the same code
  await until(() => copy.client.status === 'failed', 10000, 'refusal');
  const u = new URL(link);
  const forged = makePhone(u.origin + '/#p=' + u.hash.slice(3, 15) + '.' + 'B'.repeat(42) + 'A', 'forger');
  await until(() => forged.client.status === 'failed', 10000, 'forged secret refused');
  assert.equal(first.client.status, 'connected');
});
