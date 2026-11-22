// Two phones on one TV/PC (local controller): India's captain and Pakistan's
// captain each pair with their own link (the QR code's address), play a
// whole short match against each other, and each phone loses its connection
// once and comes back:
//   * phone 2 goes offline (Wi-Fi off) -> phone 1 sees "waiting for the other
//     team's phone", the match waits; phone 2 back online -> reconnects to
//     the same place and the match carries on,
//   * phone 1 reloads the page (browser closed and opened) -> same pairing,
//     no new QR scan,
//   * both phones show the same result, and the scorecard adds up.
// Uses real Chromium emulating two Android phones (Pixel 7) against the real
// cricket_controller_host. Not physical phones.
//
// Run: node controller/web/tests/two_phones.e2e.mjs [screenshotDir]
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium, devices } = require('playwright');
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../..');
const hostBin = process.env.CU_HOST_BIN || path.join(root, 'build/cricket_controller_host');
const shots = process.argv[2] || null;
if (shots) fs.mkdirSync(shots, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log((ok ? 'PASS ' : 'FAIL ') + what); if (!ok) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Like a real phone on home Wi-Fi: the links use a network NAME, not
// 127.0.0.1, so the page is plain http on a non-local address and is NOT a
// "secure context" in Chrome (some browser features are switched off there,
// exactly as on a real phone opening http://192.168.x.x). Chromium is told
// that this name means this machine.
const ip = 'phone-lan.test';
const host = spawn(hostBin, ['--port', '0', '--bind', '127.0.0.1', '--host-ip', ip, '--print-links', '--quiet',
  '--humans', '2', '--dev-format', 'overs=2', '--dev-fast', '--seed', '7']);
const links = [];
let buf = '';
host.stdout.on('data', (d) => {
  buf += d;
  let i;
  while ((i = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, i); buf = buf.slice(i + 1);
    if (line.startsWith('LINK ')) links.push(JSON.parse(line.slice(5)));
  }
});
const linkFor = async (role, side) => {
  for (let t = 0; t < 200; t++) {
    const l = links.find((x) => x.role === role && x.side === side);
    if (l) return l.link;
    await sleep(25);
  }
  throw new Error('no link for ' + role + ' ' + side);
};

// One phone's player: taps whatever its screen offers.
function player(page, name) {
  let n = 0;
  const used = {};
  const tapIf = async (selector) => {
    const el = page.locator(selector).first();
    if (await el.count() && await el.isVisible() && await el.isEnabled()) return el.tap({ timeout: 1500 }).then(() => true, () => false);
    return false;
  };
  const step = async () => {
    const st = await page.evaluate(() => window.CUApp && window.CUApp.state);
    if (!st || (await page.evaluate(() => window.CUApp.client.status)) !== 'connected') return;
    const c = st.controls || [];
    let did = null;
    if (c.includes('toss-decide')) did = (await tapIf('button:text("Bat first")')) && 'toss-decide';
    else if (c.includes('toss-call')) did = (await tapIf('button:text("Heads")')) && 'toss-call';
    else if (c.includes('openers')) {
      const list = page.locator('.list button');
      await list.nth(0).tap({ timeout: 1500 }).catch(() => {}); await list.nth(1).tap({ timeout: 1500 }).catch(() => {});
      did = (await tapIf('button:text("Send them in")')) && 'openers';
    } else if (c.includes('batter')) did = (await tapIf('.list button')) && 'batter';
    else if (c.includes('bowler')) did = (await tapIf('.list button')) && 'bowler';
    else if (c.includes('review')) did = (await tapIf('button:text("Accept")')) && 'review';
    else if (c.includes('bowl')) {
      await page.locator('.pitch').tap({ position: { x: 120 + (n * 31) % 70, y: 70 + (n * 47) % 50 }, timeout: 1500 }).catch(() => {});
      did = (await tapIf('button:text("BOWL")')) && 'bowl';
    } else if (c.includes('bat')) {
      const left = await page.evaluate(() => window.CUApp.ballMsLeft());
      await sleep(Math.max(0, (left == null ? 600 : left) - 20));
      did = (await tapIf('button:text("' + ['drive', 'defend', 'cut', 'pull', 'loft'][n % 5] + '")')) && 'bat';
    } else if (c.includes('run')) did = (await tapIf('.grid4 button:text-is("' + (n % 3 === 0 ? 'WAIT' : '1') + '")')) && 'run';
    else if (c.includes('fielding')) { await sleep(250); did = (await tapIf('.card button.big')) && 'fielding'; }
    else if (c.includes('ready')) did = (await tapIf('button:text("Ready to play")')) && 'ready';
    if (did) { used[did] = (used[did] || 0) + 1; n++; }
  };
  return { step, used, name };
}

// (The explicit "bypass" keeps any proxy configured on the test machine out
// of the way, so the phone talks to the host directly like on home Wi-Fi.)
const browser = await chromium.launch({ proxy: { server: 'http://127.0.0.1:9', bypass: ip },
  args: ['--host-resolver-rules=MAP ' + ip + ' 127.0.0.1'] });
try {
  const ctx1 = await browser.newContext({ ...devices['Pixel 7'] });
  const ctx2 = await browser.newContext({ ...devices['Pixel 7'] });
  const p1 = await ctx1.newPage(), p2 = await ctx2.newPage();
  const errors = [];
  for (const p of [p1, p2]) {
    p.on('pageerror', (e) => errors.push(String(e)));
    p.on('console', (m) => { if (m.type() === 'error' && !/ERR_INTERNET_DISCONNECTED|WebSocket/.test(m.text())) errors.push(m.text()); });
  }
  // ---- pairing ----
  await p1.goto(await linkFor('captain', 0));
  await p2.goto(await linkFor('captain', 1));
  for (const p of [p1, p2]) await p.waitForFunction(() => window.CUApp && window.CUApp.client.status === 'connected', null, { timeout: 10000 });
  check(true, 'both phones paired from their links (http://' + ip + ')');
  check((await p1.evaluate(() => window.isSecureContext)) === false, 'works on a plain-http LAN address (not a secure context, like a real phone on Wi-Fi)');
  const dev1 = await p1.evaluate(() => window.CUApp.client.dev);
  const dev2 = await p2.evaluate(() => window.CUApp.client.dev);
  check(dev1 && dev2 && dev1 !== dev2, 'each phone has its own device key');
  await p1.waitForFunction(() => window.CUApp.state && window.CUApp.state.teams);
  await p2.waitForFunction(() => window.CUApp.state && window.CUApp.state.teams);
  const who1 = await p1.textContent('#who'), who2 = await p2.textContent('#who');
  check(/captain/.test(who1) && /captain/.test(who2) && who1 !== who2, 'phones show different teams: "' + who1 + '" / "' + who2 + '"');
  check((await p1.evaluate(() => window.CUApp.client.side)) === 0 && (await p2.evaluate(() => window.CUApp.client.side)) === 1,
    'phone 1 = side 0, phone 2 = side 1');
  if (shots) { await p1.screenshot({ path: path.join(shots, 'two-phones-1.png') }); await p2.screenshot({ path: path.join(shots, 'two-phones-2.png') }); }

  const a = player(p1, 'phone 1'), b = player(p2, 'phone 2');
  const done = async () => (await p1.evaluate(() => !!(window.CUApp.state && window.CUApp.state.result))) &&
    (await p2.evaluate(() => !!(window.CUApp.state && window.CUApp.state.result)));
  const ballsBowled = async () => p1.evaluate(() => { const s = window.CUApp.state && window.CUApp.state.score; return s ? s.balls ?? null : null; });
  const legal = async () => p1.evaluate(() => { const c = window.CUApp.state && window.CUApp.state.scorecard; return c ? c.reduce((t, i) => t + i.bowling.length, 0) : 0; });
  void ballsBowled; void legal;
  const total = async (p) => p.textContent('#total');

  let phase = 0, sawWaiting = false, matchWaited = false, reconnected2 = false, reloaded1 = false;
  const deadline = Date.now() + 8 * 60 * 1000;
  while (Date.now() < deadline && !(await done())) {
    await a.step();
    await b.step();
    const used = (a.used.bowl || 0) + (b.used.bowl || 0);
    if (phase === 0 && used >= 4) {
      // ---- phone 2 loses its connection (Wi-Fi off) ----
      phase = 1;
      await ctx2.setOffline(true);
      await p2.evaluate(() => window.CUApp.client.ws && window.CUApp.client.ws.close());
      await p2.waitForFunction(() => window.CUApp.client.status === 'reconnecting', null, { timeout: 5000 });
      check((await p2.textContent('#who')).includes('Reconnecting'), 'phone 2 shows "Reconnecting…"');
      try {
        await p1.waitForFunction(() => window.CUApp.state && window.CUApp.state.waitingForController === 1, null, { timeout: 8000 });
        sawWaiting = (await p1.textContent('#panel')).includes('Waiting for the other team');
      } catch { sawWaiting = false; }
      if (shots) await p1.screenshot({ path: path.join(shots, 'two-phones-waiting.png') });
      const before = await total(p1);
      const stBefore = JSON.stringify(await p1.evaluate(() => window.CUApp.state.score));
      await sleep(3000);
      matchWaited = (await total(p1)) === before && JSON.stringify(await p1.evaluate(() => window.CUApp.state.score)) === stBefore;
      await ctx2.setOffline(false);
      await p2.waitForFunction(() => window.CUApp.client.status === 'connected', null, { timeout: 20000 });
      reconnected2 = (await p2.evaluate(() => window.CUApp.client.dev)) === dev2 && (await total(p2)) === before;
      await p1.waitForFunction(() => window.CUApp.state && window.CUApp.state.waitingForController !== 1, null, { timeout: 8000 }).catch(() => {});
    }
    if (phase === 1 && used >= 9) {
      // ---- phone 1: page reloaded (browser closed and opened again) ----
      phase = 2;
      await p1.reload();
      await p1.waitForFunction(() => window.CUApp && window.CUApp.client.status === 'connected', null, { timeout: 15000 });
      reloaded1 = (await p1.evaluate(() => window.CUApp.client.dev)) === dev1;
    }
    await sleep(50);
  }
  check(sawWaiting, 'phone 1 shows "Waiting for the other team’s phone to reconnect…"');
  check(matchWaited, 'the match waited (score unchanged) while phone 2 was away');
  check(reconnected2, 'phone 2 came back to the same pairing and the same score');
  check(reloaded1, 'phone 1 reload resumed the same pairing without a new QR scan');
  check(await done(), 'match finished on both phones');
  console.log('  phone 1 used ' + JSON.stringify(a.used) + '\n  phone 2 used ' + JSON.stringify(b.used));
  check((a.used.bowl || 0) > 0 && (b.used.bowl || 0) > 0 && (a.used.bat || 0) > 0 && (b.used.bat || 0) > 0,
    'both phones bowled and batted');
  const r1 = await p1.evaluate(() => window.CUApp.state.result), r2 = await p2.evaluate(() => window.CUApp.state.result);
  check(r1 === r2 && /won by|tied/.test(r1), 'both phones show the same result: ' + r1);
  const card = await p1.evaluate(() => window.CUApp.state.scorecard);
  const card2 = await p2.evaluate(() => window.CUApp.state.scorecard);
  check(JSON.stringify(card) === JSON.stringify(card2), 'both phones have the same scorecard');
  for (const inn of card || []) {
    const bat = inn.batting.reduce((t, x) => t + x.r, 0);
    const bowlRuns = inn.bowling.reduce((t, x) => t + x.r, 0);
    check(bat + inn.extras.total === inn.total && bowlRuns + inn.extras.b + inn.extras.lb + inn.extras.pen === inn.total,
      inn.team + ' ' + inn.total + '/' + inn.wickets + ': batters + extras = total = bowlers + byes + leg byes');
  }
  if (card && card.length === 2) {
    const [x, y] = card;
    const want = y.total > x.total ? y.team + ' won by ' + (10 - y.wickets) + (10 - y.wickets === 1 ? ' wicket' : ' wickets')
      : x.total > y.total ? x.team + ' won by ' + (x.total - y.total) + (x.total - y.total === 1 ? ' run' : ' runs') : 'Match tied';
    check(r1 === want, 'result matches the scores (' + want + ')');
  }
  if (shots) { await p1.screenshot({ path: path.join(shots, 'two-phones-result-1.png') }); await p2.screenshot({ path: path.join(shots, 'two-phones-result-2.png') }); }
  check(errors.length === 0, 'no JavaScript errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
} catch (e) {
  console.log('FAIL ' + e.stack);
  failures++;
} finally {
  await browser.close();
  host.kill();
}
console.log(failures ? failures + ' FAILED' : 'ALL PASSED');
process.exit(failures ? 1 : 0);
