// End-to-end tests of online play over real sockets, with real processes:
//  * the real cricket_server binary
//  * two players using the browser client code (online.js) from Node
//  * a "bad internet" TCP proxy that adds delay and cuts connections
// Covers: guest accounts, private rooms, ranked matchmaking, full matches,
// automatic reconnection, a server crash + restart mid-match, duplicate
// message protection and wrong tokens.
// Run: node --test online/web/tests/online.test.mjs  (needs build/cricket_server)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { OnlineClient } = require('../online.js');
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../..');
const serverBin = process.env.CU_SERVER_BIN || path.join(root, 'build/cricket_server');

function startServer(dataDir, extra = []) {
  const proc = spawn(serverBin, ['--port', '0', '--bind', '127.0.0.1', '--data', dataDir, '--quiet', '--dev-short-matches', ...extra]);
  return new Promise((resolve, reject) => {
    let buf = '';
    proc.stdout.on('data', (d) => {
      buf += d;
      const m = /LISTENING (\d+)/.exec(buf);
      if (m) resolve({ proc, port: Number(m[1]) });
    });
    proc.on('exit', (code) => reject(new Error('server exited ' + code)));
  });
}

// TCP proxy: adds `delayMs` each way; cut() drops every connection.
function badInternet(targetPort, delayMs) {
  const socks = new Set();
  const server = net.createServer((client) => {
    const up = net.connect(targetPort, '127.0.0.1');
    socks.add(client); socks.add(up);
    const pipe = (from, to) => from.on('data', (d) => setTimeout(() => { if (!to.destroyed) to.write(d); }, delayMs));
    pipe(client, up); pipe(up, client);
    const end = () => { client.destroy(); up.destroy(); socks.delete(client); socks.delete(up); };
    client.on('close', end); up.on('close', end); client.on('error', end); up.on('error', end);
  });
  return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({
    port: server.address().port,
    cut: () => { for (const s of socks) s.destroy(); socks.clear(); },
    close: () => { for (const s of socks) s.destroy(); server.close(); },
  })));
}

const memStorage = () => {
  const m = new Map();
  return { getItem: (k) => (m.has(k) ? m.get(k) : null), setItem: (k, v) => m.set(k, String(v)), removeItem: (k) => m.delete(k) };
};

// A player: the real client plus simple decisions made from server state only.
function player(port, name) {
  const p = { events: [], state: null, ended: null, lastSig: '', acks: 0, nacks: 0, statuses: [] };
  p.client = new OnlineClient({ url: 'ws://127.0.0.1:' + port + '/ws', storage: memStorage(), WebSocket });
  p.client.listen((type, data) => {
    if (type === 'status') p.statuses.push(data.status);
    if (type === 'state') { p.state = data; decide(p); }
    if (type === 'ball') {
      // Tap when the ball arrives (+/- a little), and report honest timing.
      const aim = data.arriveInMs + Math.round(Math.random() * 200 - 100);
      const t0 = Date.now();
      setTimeout(() => {
        if (!p.state || !(p.state.controls || []).includes('bat')) return;
        p.client.act({ do: 'bat', shot: 'drive', dir: 10, timing: Date.now() - t0 - data.arriveInMs });
      }, Math.max(0, aim));
    }
    if (type === 'ack') { if (data.ok) p.acks++; else p.nacks++; }
    if (type === 'end') p.ended = data;
    p.events.push(type);
  });
  p.client.createGuest(name, 'IN');
  return p;
}

function decide(p) {
  const s = p.state;
  const sig = s.waiting + '|' + s.stage + '|' + JSON.stringify(s.controls) + JSON.stringify(s.score || {});
  if (sig === p.lastSig) return;
  p.lastSig = sig;
  const has = (c) => (s.controls || []).includes(c);
  const first = (l) => s.options && s.options[l] && s.options[l].length ? s.options[l][0].id : -1;
  let a = null;
  if (has('toss-call')) a = { do: 'toss-call', face: 'heads' };
  else if (has('toss-decide')) a = { do: 'toss-decide', choice: 'bat' };
  else if (has('openers')) a = { do: 'openers', striker: s.options.batters[0].id, nonStriker: s.options.batters[1].id };
  else if (has('batter')) a = { do: 'batter', player: first('batters') };
  else if (has('bowler')) a = { do: 'bowler', player: first('bowlers') };
  else if (has('review')) a = { do: 'accept' };
  else if (has('ready')) a = { do: 'ready' };
  else if (has('bowl')) a = { do: 'bowl', variation: 'stock', line: 0, length: Math.random(), effort: 70 };
  else if (has('run')) a = { do: 'run', call: Math.random() < 0.5 ? 'wait' : '1' };  // answer at once (keeps the test quick)
  else if (has('fielding')) a = { do: 'fielding', act: (s.fieldPlay && s.fieldPlay.kind) === 'catch' ? 'catch' : 'dive', timing: 0 };
  if (a) p.client.act(a);
}

const waitFor = async (cond, ms, what) => {
  const t0 = Date.now();
  while (!cond()) {
    if (Date.now() - t0 > ms) throw new Error('timed out waiting for ' + what);
    await new Promise((r) => setTimeout(r, 25));
  }
};
const tmp = (n) => fs.mkdtempSync(path.join(os.tmpdir(), 'cu-online-' + n + '-'));

test('two players finish a private-room match through a slow, unreliable connection', { timeout: 240000 }, async () => {
  const srv = await startServer(tmp('room'));
  const proxy = await badInternet(srv.port, 60);  // ~120 ms round trip for player B
  try {
    const a = player(srv.port, 'Asha');
    const b = player(proxy.port, 'Bilal');
    await waitFor(() => a.client.status === 'lobby' && b.client.status === 'lobby', 10000, 'both in lobby');
    assert.ok(a.client.id && a.client.token, 'guest account saved');
    const teams = await a.client.request('teams');
    assert.ok(teams.ok && teams.teams.includes('India'));
    const room = await a.client.request('room-create', { format: 'overs=2', team: 'India' });
    assert.ok(room.ok, room.err);
    assert.match(room.code, /^[A-HJ-NP-Z2-9]{6}$/);
    const bad = await b.client.request('room-join', { code: room.code, team: 'India' });
    assert.ok(!bad.ok);
    const join = await b.client.request('room-join', { code: room.code.toLowerCase(), team: 'Pakistan' });
    assert.ok(join.ok, join.err);
    const start = await a.client.request('room-start');
    assert.ok(start.ok, start.err);
    await waitFor(() => a.client.inMatch && b.client.inMatch, 5000, 'match start');
    assert.equal(a.client.side, 0);
    assert.equal(b.client.side, 1);
    // Cut B's connection three times during the match: it must come back by itself.
    for (let i = 0; i < 3; i++) {
      await new Promise((r) => setTimeout(r, 4000));
      if (a.ended) break;
      proxy.cut();
    }
    await waitFor(() => a.ended && b.ended, 200000, 'match end');
    assert.equal(a.ended.record.id, b.ended.record.id);
    assert.deepEqual(a.ended.record.s0, b.ended.record.s0, 'both see the same server scorebook');
    assert.ok(b.statuses.filter((s) => s === 'reconnecting').length >= 1, 'B reconnected');
    assert.ok(a.acks > 5 && b.acks > 5);
    const hist = await b.client.request('history');
    assert.equal(hist.matches.length, 1);
    const stats = await a.client.request('stats');
    assert.equal(stats.stats.matches, 1);
    a.client.stop(); b.client.stop();
  } finally {
    proxy.close();
    srv.proc.kill();
  }
});

test('ranked matchmaking, then the server crashes mid-match and both players carry on', { timeout: 240000 }, async () => {
  const dir = tmp('crash');
  const quick = ['--ranked-unlock', '0'];  // brand-new test players may play ranked
  let srv = await startServer(dir, quick);
  const proxy = await badInternet(srv.port, 0);
  let proxyTarget = srv.port;
  try {
    const a = player(proxy.port, 'Kabir');
    const b = player(proxy.port, 'Zain');
    await waitFor(() => a.client.status === 'lobby' && b.client.status === 'lobby', 10000, 'lobby');
    assert.ok((await a.client.request('queue', { mode: 'ranked', format: 'overs=2', team: 'India' })).ok);
    assert.ok((await b.client.request('queue', { mode: 'ranked', format: 'overs=2', team: 'Pakistan' })).ok);
    await waitFor(() => a.client.inMatch && b.client.inMatch, 5000, 'matched');
    assert.equal(a.client.match.mode, 'ranked');
    await waitFor(() => a.state && a.state.score && a.state.score.balls + a.state.score.overs * 6 >= 4, 60000, 'a few balls');
    const scoreBefore = a.state.score.total;
    // Power cut: the server dies without warning, then starts again.
    srv.proc.kill('SIGKILL');
    await new Promise((r) => setTimeout(r, 300));
    srv = await startServer(dir, quick);
    proxyTarget = srv.port;
    proxy.close();
    // Clients reconnect to the same address: put a proxy back on that port.
    const again = net.createServer((c) => { const u = net.connect(proxyTarget, '127.0.0.1'); c.pipe(u); u.pipe(c); c.on('error', () => u.destroy()); u.on('error', () => c.destroy()); });
    await new Promise((r) => again.listen(proxy.port, '127.0.0.1', r));
    try {
      await waitFor(() => a.client.status === 'connected' && b.client.status === 'connected', 20000, 'reconnect after restart');
      await waitFor(() => a.state && a.state.score && a.state.score.total >= scoreBefore, 5000, 'state restored');
      await waitFor(() => a.ended && b.ended, 200000, 'match end');
      assert.equal(a.ended.record.mode, 'ranked');
      if (a.ended.record.winner >= 0) {
        assert.notEqual(a.ended.delta, 0, 'ranked result changes ratings');
        assert.equal(a.ended.delta, -b.ended.delta);
      }
      const board = await a.client.request('leaderboard', { board: 'rating', mode: 'ranked' });
      assert.equal(board.board.rows.length, 2);
      a.client.stop(); b.client.stop();
    } finally {
      again.close();
    }
  } finally {
    proxy.close();
    srv.proc.kill();
  }
});

test('protocol guards: wrong token, repeated message numbers, unknown requests', { timeout: 30000 }, async () => {
  const srv = await startServer(tmp('guards'));
  try {
    const open = () => new Promise((resolve) => {
      const ws = new WebSocket('ws://127.0.0.1:' + srv.port + '/ws');
      const msgs = [];
      ws.onmessage = (e) => msgs.push(JSON.parse(e.data));
      ws.onopen = () => resolve({ ws, msgs, closed: new Promise((r) => { ws.onclose = r; }) });
    });
    const x = await open();
    x.ws.send(JSON.stringify({ s: 1, k: 'login', id: 'gNOBODY', token: 'x' }));
    await x.closed;
    assert.ok(x.msgs.some((m) => m.k === 'res' && !m.ok));

    const y = await open();
    y.ws.send(JSON.stringify({ s: 1, k: 'guest', name: '<b>Hacker</b>', country: 'IN' }));
    await waitFor(() => y.msgs.length > 0, 3000, 'guest reply');
    assert.equal(y.msgs[0].name, '<b>Hacker</b>');  // stored as plain text; the page shows it with textContent
    y.ws.send(JSON.stringify({ s: 2, k: 'what' }));
    await waitFor(() => y.msgs.some((m) => m.re === 2), 3000, 'unknown reply');
    assert.equal(y.msgs.find((m) => m.re === 2).err, 'unknown request');
    y.ws.send(JSON.stringify({ s: 2, k: 'teams' }));  // repeated number
    await y.closed;
    assert.equal(y.msgs[y.msgs.length - 1].why, 'duplicate or out-of-order message');
  } finally {
    srv.proc.kill();
  }
});
