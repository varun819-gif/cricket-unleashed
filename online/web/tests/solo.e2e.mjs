// Single player vs computer in a desktop Chrome window (laptop size), against
// the real cricket_server: guest sign-in, pick India, play a full 2-over
// match by clicking (toss, batting, bowling), live scoreboard, result.
//
// Run: node online/web/tests/solo.e2e.mjs [screenshotDir]
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../..');
const serverBin = process.env.CU_SERVER_BIN || path.join(root, 'build/cricket_server');
const shots = process.argv[2] || null;
if (shots) fs.mkdirSync(shots, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log((ok ? 'PASS ' : 'FAIL ') + what); if (!ok) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const data = fs.mkdtempSync(path.join(os.tmpdir(), 'cu-solo-'));
const server = spawn(serverBin, ['--port', '0', '--bind', '127.0.0.1', '--data', data, '--quiet', '--dev-short-matches']);
const port = await new Promise((resolve) => {
  let buf = '';
  server.stdout.on('data', (d) => { buf += d; const m = /LISTENING (\d+)/.exec(buf); if (m) resolve(m[1]); });
});
const browser = await chromium.launch();
const errors = [];
try {
  const page = await (await browser.newContext({ viewport: { width: 1366, height: 768 } })).newPage();
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto('http://127.0.0.1:' + port + '/');
  await page.fill('#gName', 'Varun');
  await page.selectOption('#gCountry', 'IN');
  await page.click('#gStart');
  await page.waitForFunction(() => window.CUOnlineClient.status === 'lobby');
  await page.waitForFunction(() => document.querySelectorAll('#opponent option').length > 0);
  await page.selectOption('#team', 'India');
  check(await page.inputValue('#opponent') === 'Pakistan', 'computer plays Pakistan by default');
  check(await page.inputValue('#format') === 't20', 'T20 selected by default');
  await page.selectOption('#format', 'overs=2');  // test-only short match (server started with --dev-short-matches)
  if (shots) await page.screenshot({ path: path.join(shots, 'solo-lobby.png'), fullPage: true });
  // Record every state the page receives (messages change quickly).
  await page.evaluate(() => {
    window.__msgs = [];
    window.CUOnlineClient.listen((t, d) => {
      if (t === 'state') {
        window.__msgs.push((d.message || '') + ' | ' + (d.lastBall ? d.lastBall.summary : ''));
        if (d.scorecard) window.__card = d.scorecard;
        if (d.conditions) window.__cond = d.conditions;
        if (d.fieldPlay) window.__fieldPlays = (window.__fieldPlays || 0) + 1;
        if (d.runPlay) window.__runPlays = (window.__runPlays || 0) + 1;
      }
    });
  });
  await page.click('#solo');
  await page.waitForFunction(() => window.CUOnlineClient.status === 'connected', null, { timeout: 5000 });
  check((await page.textContent('#who')).includes('India'), 'I am India');
  check(await page.locator('#lobby').isHidden(), 'match screen shown');

  const seen = new Set();
  const scores = new Set();
  const messages = new Set();
  let shotBowl = false, shotBat = false, shotField = false, steppedOut = false, fielded = 0, ranCalls = 0;
  const click = async (sel) => {
    const el = page.locator(sel).first();
    // Short timeout: if the screen just changed, the loop simply tries again.
    if (await el.count() && await el.isVisible() && await el.isEnabled()) return el.click({ timeout: 2000 }).then(() => true, () => false);
    return false;
  };
  const deadline = Date.now() + 180000;
  while (Date.now() < deadline) {
    if (await page.evaluate(() => !!window.CULobby.ended)) break;
    const st = await page.evaluate(() => window.CUApp.state);
    const c = (st && st.controls) || [];
    c.forEach((x) => seen.add(x));
    if (st && st.message) messages.add(st.message);
    const total = await page.textContent('#total');
    if (total) scores.add(total);
    if (c.includes('toss-decide')) await click('button:text("Bat first")');
    else if (c.includes('toss-call')) await click('button:text("Heads")');
    else if (c.includes('openers')) {
      const list = page.locator('.list button');
      await list.nth(0).click({ timeout: 2000 }).catch(() => {}); await list.nth(1).click({ timeout: 2000 }).catch(() => {});
      await click('button:text("Send them in")');
    } else if (c.includes('batter') || c.includes('bowler')) await click('.list button');
    else if (c.includes('review')) await click('button:text("Accept")');
    else if (c.includes('bowl')) {
      if (shots && !shotBowl) { await page.screenshot({ path: path.join(shots, 'solo-bowling.png') }); shotBowl = true; }
      await page.locator('.pitch').click({ position: { x: 150, y: 90 }, timeout: 2000 }).catch(() => {});
      await click('button:text("BOWL")');
    } else if (c.includes('bat')) {
      if (shots && !shotBat) { await page.screenshot({ path: path.join(shots, 'solo-batting.png') }); shotBat = true; }
      if (!steppedOut && await click('button:text("Step out")')) steppedOut = true;
      await sleep(1000);  // wait for the ball to arrive
      await click('button:text("drive")');
    } else if (c.includes('run')) {
      if (await click('.grid4 button:text-is("1")')) ranCalls++;
    } else if (c.includes('fielding')) {
      if (shots && !shotField) { await page.screenshot({ path: path.join(shots, 'solo-fielding.png') }); shotField = true; }
      fielded++;
      await click('button:text("Throw: keeper end")');
      await sleep(500);  // let the ball reach the fielder
      await click('.card button.big');
    }
    await sleep(80);
  }
  // The toss: I call or choose when it's my turn; otherwise the computer
  // captain does and the result is announced.
  const msgs = await page.evaluate(() => window.__msgs);
  check(seen.has('toss-call') || seen.has('toss-decide') || msgs.some((m) => /toss/i.test(m)), 'toss happened and was shown');
  check(msgs.some((m) => /km\/h, (pitched|full toss)/.test(m)), 'each ball shows its physics (speed, length, movement, shot)');
  console.log('  e.g. ' + msgs.filter((m) => /km\/h/.test(m)).slice(-3).join('\n       '));
  check(seen.has('bowl'), 'I bowled (the computer batted)');
  check(seen.has('bat'), 'I batted (the computer bowled)');
  check([...scores].some((s) => /^India \d+\/\d+ \(\d\.\d\)$/.test(s)) && [...scores].some((s) => /^Pakistan \d+\/\d+ \(\d\.\d\)$/.test(s)),
    'live scoreboard showed runs/wickets (overs) for both teams');
  // Whether the computer hits the ball to our fielders in 2 overs depends on
  // the match; when it does, the prompt must appear and work.
  const plays = await page.evaluate(() => window.__fieldPlays || 0);
  check(plays === 0 || fielded >= 1, 'fielding prompt shown and used when the ball came to a fielder (' + fielded + ' of ' + plays + ' states)');
  const runPlays = await page.evaluate(() => window.__runPlays || 0);
  check(runPlays === 0 || ranCalls >= 1, 'Run? card shown and a run called when my batters had a run to judge (' + ranCalls + ')');
  check(steppedOut, 'stepped down the pitch with the Step out button');
  check(msgs.some((m) => /down the pitch/.test(m)), 'the physics played the shot down the pitch');
  const cond = await page.evaluate(() => window.__cond);
  check(cond && /^(standard|green|dusty|bouncy|flat)$/.test(cond.pitch), 'match conditions sent (pitch ' + (cond && cond.pitch) + ')');
  // Scorecard during / after the match.
  await page.waitForSelector('#endCard', { state: 'visible', timeout: 5000 });
  const result = await page.textContent('#endText');
  check(/won by|tied/i.test(result), 'result shown: ' + result);
  check((await page.textContent('#endRating')).includes('Single player'), 'marked as single player (no rating change)');
  if (shots) await page.screenshot({ path: path.join(shots, 'solo-result.png'), fullPage: true });
  // The scorecard of the match just played (from the last match state).
  const card = await page.evaluate(() => window.__card);
  check(Array.isArray(card) && card.length === 2 && card[0].batting.length > 0 && card[0].bowling.length > 0,
    'scorecard data: batting, bowling for both innings');
  check(card && card.every((c) => c.batting.reduce((a, b) => a + b.r, 0) + c.extras.total === c.total),
    'scorecard adds up (batters + extras = total)');
  await page.click('#endOk');
  await page.click('#tabHistory');
  await page.waitForFunction(() => document.querySelector('#tabBody').textContent.includes('vs computer'));
  check(true, 'history lists the match as "vs computer"');
  check(errors.length === 0, 'no browser errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
} catch (e) {
  console.log('FAIL ' + e.stack);
  failures++;
} finally {
  await browser.close();
  server.kill();
}
console.log(failures ? failures + ' FAILED' : 'ALL PASSED');
process.exit(failures ? 1 : 0);
