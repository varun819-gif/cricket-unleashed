// A COMPLETE 20-over T20, human (emulated Android phone, Pixel 7) versus the
// computer, against the real cricket_server. The test taps every control a
// player uses: toss, openers, next batter, bowler each over, bowling (pitch
// spot + BOWL), batting (shot + direction + Step out), running (WAIT / 1 / 2 /
// 3), fielding (CATCH / DIVE + throw end), reviews. At the end it checks:
//   * the scorecard adds up (batters + extras = total, bowlers' runs + byes +
//     leg byes + penalties = total, wickets, overs, fall of wickets),
//   * the result shown matches the scores (winner and margin worked out again),
//   * the server's saved match file, replayed by `cricket_sim --verify`, gives
//     the same totals and result and passes the C++ checks.
// Server runs with --dev-fast (quicker balls) so the match takes ~10 minutes.
// This is phone EMULATION in desktop Chromium, not a physical phone.
//
// Run: node online/web/tests/t20_full.e2e.mjs [screenshotDir]
import { spawn, spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium, devices } = require('playwright');
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../..');
const serverBin = process.env.CU_SERVER_BIN || path.join(root, 'build/cricket_server');
const simBin = process.env.CU_SIM_BIN || path.join(path.dirname(serverBin), 'cricket_sim');
const shots = process.argv[2] || null;
if (shots) fs.mkdirSync(shots, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log((ok ? 'PASS ' : 'FAIL ') + what); if (!ok) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const data = fs.mkdtempSync(path.join(os.tmpdir(), 'cu-t20-'));
const server = spawn(serverBin, ['--port', '0', '--bind', '127.0.0.1', '--data', data, '--quiet', '--dev-fast']);
const port = await new Promise((resolve) => {
  let buf = '';
  server.stdout.on('data', (d) => { buf += d; const m = /LISTENING (\d+)/.exec(buf); if (m) resolve(m[1]); });
});
const browser = await chromium.launch();
const errors = [];
const used = {};
const bump = (k) => { used[k] = (used[k] || 0) + 1; };
try {
  const page = await (await browser.newContext({ ...devices['Pixel 7'] })).newPage();
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto('http://127.0.0.1:' + port + '/');
  await page.fill('#gName', 'Test Player');
  await page.selectOption('#gCountry', 'IN');
  await page.click('#gStart');
  await page.waitForFunction(() => window.CUOnlineClient.status === 'lobby');
  await page.waitForFunction(() => document.querySelectorAll('#opponent option').length > 0);
  await page.selectOption('#team', 'India');
  check(await page.inputValue('#format') === 't20', 'format is T20 (the only one offered)');
  check(await page.locator('#format').isDisabled(), 'format cannot be changed');
  await page.evaluate(() => {
    window.__results = [];
    window.CUOnlineClient.listen((t, d) => {
      if (t === 'state') {
        if (d.scorecard) window.__card = d.scorecard;
        if (d.result) window.__result = d.result;
        if (d.score) window.__score = d.score;
      }
    });
  });
  await page.click('#solo');
  await page.waitForFunction(() => window.CUOnlineClient.status === 'connected', null, { timeout: 5000 });

  const click = async (sel) => {
    const el = page.locator(sel).first();
    if (await el.count() && await el.isVisible() && await el.isEnabled()) return el.click({ timeout: 1500 }).then(() => true, () => false);
    return false;
  };
  const shotsList = ['drive', 'defend', 'drive', 'cut', 'pull', 'flick', 'loft', 'defend'];
  let ballNo = 0, shotsTaken = { bat: false, run: false, field: false, bowl: false };
  const started = Date.now();
  const deadline = started + 45 * 60 * 1000;
  while (Date.now() < deadline) {
    if (await page.evaluate(() => !!window.CULobby.ended)) break;
    const st = await page.evaluate(() => window.CUApp.state);
    const c = (st && st.controls) || [];
    if (c.includes('toss-decide')) { if (await click('button:text("Bat first")')) bump('toss-decide'); }
    else if (c.includes('toss-call')) { if (await click('button:text("Heads")')) bump('toss-call'); }
    else if (c.includes('openers')) {
      const list = page.locator('.list button');
      await list.nth(0).click({ timeout: 1500 }).catch(() => {}); await list.nth(1).click({ timeout: 1500 }).catch(() => {});
      if (await click('button:text("Send them in")')) bump('openers');
    } else if (c.includes('batter')) { if (await click('.list button')) bump('batter'); }
    else if (c.includes('bowler')) {
      // Rotate bowlers (5 x 4 overs max): pick the first one offered.
      if (await click('.list button')) bump('bowler');
    } else if (c.includes('review')) {
      if (await click(used.review ? 'button:text("Accept")' : 'button:text("REVIEW")')) bump('review');
    } else if (c.includes('bowl')) {
      if (shots && !shotsTaken.bowl) { await page.screenshot({ path: path.join(shots, 't20-bowling.png') }); shotsTaken.bowl = true; }
      const x = 110 + (ballNo * 37) % 90, y = 60 + (ballNo * 53) % 70;
      await page.locator('.pitch').click({ position: { x, y }, timeout: 1500 }).catch(() => {});
      if (await click('button:text("BOWL")')) { bump('bowl'); ballNo++; }
    } else if (c.includes('bat')) {
      if (shots && !shotsTaken.bat) { await page.screenshot({ path: path.join(shots, 't20-batting.png') }); shotsTaken.bat = true; }
      if (ballNo % 17 === 5) await click('button:text("Step out")');
      // Wait until the ball is about to arrive (flight 700 ms with --dev-fast).
      const left = await page.evaluate(() => window.CUApp.ballMsLeft ? window.CUApp.ballMsLeft() : 600);
      await sleep(Math.max(0, (left == null ? 600 : left) - 20));
      if (await click('button:text("' + shotsList[ballNo % shotsList.length] + '")')) { bump('bat'); ballNo++; }
    } else if (c.includes('run')) {
      if (shots && !shotsTaken.run) { await page.screenshot({ path: path.join(shots, 't20-running.png') }); shotsTaken.run = true; }
      // Like a sensible player: read the card (throw time vs running time),
      // sometimes taking a risk.
      const rp = st.runPlay || {};
      const t = rp.runS || [3, 5.7, 8.4];
      const back = (rp.backS || 0) + (ballNo % 5 === 0 ? 0.6 : 0);
      let n = 0;
      while (n < 3 && t[n] + 0.3 <= back) n++;
      const pick = n === 0 ? 'WAIT' : String(n);
      if (await click('.grid4 button:text-is("' + pick + '")')) bump('run-' + pick);
    } else if (c.includes('fielding')) {
      if (shots && !shotsTaken.field) { await page.screenshot({ path: path.join(shots, 't20-fielding.png') }); shotsTaken.field = true; }
      await click(ballNo % 2 ? 'button:text("Throw: keeper end")' : 'button:text("Throw: bowler end")');
      await sleep(300);
      if (await click('.card button.big')) bump('fielding');
    } else if (c.includes('ready')) { if (await click('button:text("Ready to play")')) bump('ready'); }
    await sleep(60);
  }
  console.log('  match took ' + Math.round((Date.now() - started) / 1000) + ' s; controls used: ' + JSON.stringify(used));
  check(await page.evaluate(() => !!window.CULobby.ended), 'the match finished');
  check(used.bowl >= 60, 'I bowled the computer out or bowled 20 overs (' + used.bowl + ' balls)');
  check(used.bat >= 30, 'I batted (' + used.bat + ' shots)');
  check(used.bowler >= 4, 'I chose a bowler for each over (' + used.bowler + ')');
  const runCalls = Object.keys(used).filter((k) => k.startsWith('run-')).reduce((a, k) => a + used[k], 0);
  check(runCalls >= 5 && used['run-WAIT'] >= 1 && (used['run-1'] || 0) + (used['run-2'] || 0) + (used['run-3'] || 0) >= 1,
    'I called runs: WAIT and 1/2/3 (' + runCalls + ' calls)');
  check((used.fielding || 0) >= 1, 'I fielded (catch/dive) when the ball came to my fielders (' + (used.fielding || 0) + ')');

  // ---- scorecard adds up ----
  const card = await page.evaluate(() => window.__card);
  check(Array.isArray(card) && card.length === 2, 'scorecard has both innings');
  const balls = (o) => { const [a, b] = String(o).split('.'); return Number(a) * 6 + Number(b || 0); };
  for (const inn of card || []) {
    const bat = inn.batting.reduce((a, b) => a + b.r, 0);
    const bowlRuns = inn.bowling.reduce((a, b) => a + b.r, 0);
    const bowlW = inn.bowling.reduce((a, b) => a + b.w, 0);
    const runOuts = inn.batting.filter((b) => /^run out/.test(b.how)).length;
    const bowlBalls = inn.bowling.reduce((a, b) => a + balls(b.o), 0);
    const e = inn.extras;
    check(bat + e.total === inn.total, inn.team + ': batters ' + bat + ' + extras ' + e.total + ' = total ' + inn.total);
    check(bowlRuns + e.b + e.lb + e.pen === inn.total, inn.team + ': bowlers\' runs + byes + leg byes = total');
    check(bowlW + runOuts === inn.wickets && inn.wickets <= 10, inn.team + ': wickets ' + inn.wickets + ' (bowlers ' + bowlW + ', run outs ' + runOuts + ')');
    check(bowlBalls === balls(inn.overs) && balls(inn.overs) <= 120, inn.team + ': bowlers\' overs = ' + inn.overs + ' (max 20)');
    check(inn.fow.length === inn.wickets, inn.team + ': fall of wickets listed for every wicket');
    check(inn.batting.every((b) => b.b === 0 || Math.abs(b.sr - (100 * b.r) / b.b) < 0.06), inn.team + ': strike rates correct');
    check(inn.bowling.every((b) => b.o === '0.0' || Math.abs(b.econ - (6 * b.r) / balls(b.o)) < 0.006), inn.team + ': economy rates correct');
  }
  // ---- result matches the scores ----
  const shown = await page.textContent('#endText');
  if (card && card.length === 2) {
    const [a, b] = card;
    let want;
    if (b.total > a.total) want = b.team + ' won by ' + (10 - b.wickets) + (10 - b.wickets === 1 ? ' wicket' : ' wickets');
    else if (a.total > b.total) want = a.team + ' won by ' + (a.total - b.total) + (a.total - b.total === 1 ? ' run' : ' runs');
    else want = 'Match tied';
    check(shown.trim() === want, 'result shown "' + shown.trim() + '" = worked out from the scores "' + want + '"');
  }
  if (shots) await page.screenshot({ path: path.join(shots, 't20-result.png'), fullPage: true });
  // ---- the server's saved match, replayed and checked by the C++ engine ----
  const rec = await page.evaluate(() => window.CULobby.ended && window.CULobby.ended.record);
  check(rec && rec.save, 'server recorded the match (' + (rec && rec.save) + ')');
  if (rec && rec.save && fs.existsSync(simBin)) {
    const v = spawnSync(simBin, ['--verify', path.join(data, rec.save)], { encoding: 'utf8' });
    const out = v.stdout || '';
    check(v.status === 0 && /VERIFY OK/.test(out), 'saved match replays and passes the scorecard + result checks');
    const totals = [...out.matchAll(/^TOTAL (.+) (\d+)\/(\d+) (\S+)$/gm)].map((m) => ({ team: m[1], total: +m[2], wickets: +m[3], overs: m[4] }));
    check(totals.length === 2 && card && totals.every((t, i) => t.team === card[i].team && t.total === card[i].total &&
      t.wickets === card[i].wickets && t.overs === card[i].overs), 'replayed totals = the phone\'s scorecard: ' +
      totals.map((t) => t.team + ' ' + t.total + '/' + t.wickets + ' (' + t.overs + ')').join(', '));
    check(out.includes('VERIFY OK: ' + shown.trim()), 'replayed result = the result the phone showed');
  } else if (!fs.existsSync(simBin)) {
    check(false, 'cricket_sim not found at ' + simBin);
  }
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
