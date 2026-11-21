// Two-browser online test: two emulated Android phones (Pixel 7, desktop
// Chromium emulation - not physical devices) use the real online page against
// the real cricket_server: guest sign-in, private room by code, a full match
// played by tapping, a page reload mid-match, result, stats, leaderboard.
// Both "phones" run on this one machine, so this is NOT an internet test.
//
// Run: node online/web/tests/browser.e2e.mjs [screenshotDir]
import { spawn } from 'node:child_process';
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
const shots = process.argv[2] || null;
if (shots) fs.mkdirSync(shots, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log((ok ? 'PASS ' : 'FAIL ') + what); if (!ok) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const data = fs.mkdtempSync(path.join(os.tmpdir(), 'cu-online-browser-'));
const server = spawn(serverBin, ['--port', '0', '--bind', '127.0.0.1', '--data', data, '--quiet', '--dev-short-matches']);
const port = await new Promise((resolve) => {
  let buf = '';
  server.stdout.on('data', (d) => { buf += d; const m = /LISTENING (\d+)/.exec(buf); if (m) resolve(m[1]); });
});
const url = 'http://127.0.0.1:' + port + '/';

const browser = await chromium.launch();
const errors = [];
async function phone(name, country) {
  const ctx = await browser.newContext({ ...devices['Pixel 7'] });
  const page = await ctx.newPage();
  page.on('console', (m) => { if (m.type() === 'error') errors.push(name + ': ' + m.text()); });
  page.on('pageerror', (e) => errors.push(name + ': ' + String(e)));
  await page.goto(url);
  await page.waitForSelector('#gName', { state: 'visible' });
  await page.fill('#gName', name);
  await page.selectOption('#gCountry', country);
  await page.tap('#gStart');
  await page.waitForFunction(() => window.CUOnlineClient.status === 'lobby', null, { timeout: 10000 });
  await page.waitForFunction(() => document.querySelectorAll('#team option').length > 0);
  return page;
}

// Tap whatever the match screen offers, like a person would.
async function playStep(page, done) {
  const tapIf = async (selector) => {
    const el = page.locator(selector).first();
    if (await el.count() && await el.isVisible() && await el.isEnabled()) { await el.tap(); return true; }
    return false;
  };
  const st = await page.evaluate(() => window.CUApp.state);
  const c = (st && st.controls) || [];
  if (c.includes('toss-decide')) await tapIf('button:text("Bat first")');
  else if (c.includes('toss-call')) await tapIf('button:text("Heads")');
  else if (c.includes('openers')) {
    const list = page.locator('.list button');
    await list.nth(0).tap(); await list.nth(1).tap();
    await tapIf('button:text("Send them in")');
  } else if (c.includes('batter') || c.includes('bowler')) await tapIf('.list button');
  else if (c.includes('review')) await tapIf('button:text("Accept")');
  else if (c.includes('bowl')) {
    await page.locator('.pitch').tap({ position: { x: 150, y: 90 } });
    if (await tapIf('button:text("BOWL")')) done.bowled = true;
  } else if (c.includes('bat')) {
    if (await tapIf('button:text("drive")')) done.batted = true;
  }
}

try {
  const a = await phone('Asha', 'IN');
  const b = await phone('Bilal', 'PK');
  check((await a.textContent('#me')).includes('Asha'), 'A signed in as a guest (no password)');
  check((await a.textContent('#me')).includes('India'), 'server region India shown');
  if (shots) await a.screenshot({ path: path.join(shots, 'online-lobby.png'), fullPage: true });

  await a.selectOption('#team', 'India');
  await a.selectOption('#format', 'overs=2');
  await a.tap('#createRoom');
  await a.waitForSelector('#roomCard', { state: 'visible' });
  const code = (await a.textContent('#roomCode')).trim();
  check(/^[A-HJ-NP-Z2-9]{6}$/.test(code), 'room code shown: ' + code);

  // B first tries the same team (refused), then picks Pakistan.
  await b.selectOption('#team', 'India');
  await b.fill('#code', code.toLowerCase());
  await b.tap('#joinRoom');
  await b.waitForFunction(() => document.querySelector('#toast').textContent.includes('another team'));
  check(true, 'same team refused with a clear message');
  await b.selectOption('#team', 'Pakistan');
  await b.tap('#joinRoom');
  await a.waitForFunction(() => document.querySelector('#roomPlayers').textContent.includes('Bilal'));
  check(true, 'host sees the friend join');
  check(await b.locator('#startRoom').isHidden(), 'only the host gets the Start button');
  if (shots) await a.screenshot({ path: path.join(shots, 'online-room.png'), fullPage: true });
  await a.tap('#startRoom');
  await a.waitForFunction(() => window.CUOnlineClient.status === 'connected');
  await b.waitForFunction(() => window.CUOnlineClient.status === 'connected');
  check(await a.locator('#lobby').isHidden(), 'lobby hidden during the match');
  check((await a.textContent('#who')).includes('India'), 'A plays India');
  check((await b.textContent('#who')).includes('Pakistan'), 'B plays Pakistan');

  const doneA = {}, doneB = {};
  let reloaded = false, shotMatch = false;
  const deadline = Date.now() + 240000;
  while (Date.now() < deadline) {
    const endA = await a.evaluate(() => !!window.CULobby.ended);
    const endB = await b.evaluate(() => !!window.CULobby.ended);
    if (endA && endB) break;
    await playStep(a, doneA);
    await playStep(b, doneB);
    if (shots && !shotMatch && doneA.bowled) { await a.screenshot({ path: path.join(shots, 'online-match.png') }); shotMatch = true; }
    // Mid-match, B's page reloads (phone browser killed): it signs back in by itself.
    if (!reloaded && (doneB.batted || doneB.bowled)) {
      reloaded = true;
      await b.reload();
      await b.waitForFunction(() => window.CUOnlineClient && window.CUOnlineClient.status === 'connected', null, { timeout: 15000 });
      check(true, 'page reload mid-match: back in the same match without signing in again');
    }
    await sleep(100);
  }
  check(doneA.bowled || doneB.bowled, 'bowled from a phone screen');
  check(doneA.batted || doneB.batted, 'batted from a phone screen');
  await a.waitForSelector('#endCard', { state: 'visible', timeout: 5000 });
  await b.waitForSelector('#endCard', { state: 'visible', timeout: 5000 });
  const ea = await a.textContent('#endText'), eb = await b.textContent('#endText');
  check(ea.length > 0 && ea === eb, 'both phones show the same result: ' + ea);
  check((await a.textContent('#endRating')).includes('Casual'), 'private room = casual, rating unchanged');
  if (shots) await a.screenshot({ path: path.join(shots, 'online-result.png'), fullPage: true });
  await a.tap('#endOk');
  await a.tap('#tabHistory');
  await a.waitForFunction(() => document.querySelectorAll('#tabBody tr').length === 1);
  check(true, 'history shows the match');
  await a.tap('#tabBoard');
  await a.selectOption('#board', 'matches');
  await a.waitForFunction(() => document.querySelectorAll('#tabBody tr').length === 2);
  check(true, 'leaderboard lists both players');
  await a.selectOption('#bCountry', 'PK');
  await a.waitForFunction(() => document.querySelectorAll('#tabBody tr').length === 1 && document.querySelector('#tabBody').textContent.includes('Bilal'));
  check(true, 'country leaderboard (Pakistan) shows only Bilal');
  // Flags (permitted assets) appear next to teams.
  check(await a.locator('#teamFlag').isVisible(), 'team flag shown in the lobby');
  // A reports B from the history list; an admin reviews and bans B for 24 hours.
  await a.tap('#tabHistory');
  await a.waitForSelector('#tabBody button.report');
  await a.tap('#tabBody button.report');
  await a.selectOption('#reportReason', 'abuse');
  await a.fill('#reportNote', 'test report');
  await a.tap('#reportSend');
  await a.waitForFunction(() => document.querySelector('#toast').textContent.includes('admin will review'));
  check(true, 'player report sent from the history list');
  const adminCtx = await browser.newContext();
  const adm = await adminCtx.newPage();
  adm.on('pageerror', (e) => errors.push('admin: ' + String(e)));
  await adm.goto(url + 'admin');
  await adm.fill('#token', fs.readFileSync(path.join(data, 'admin.token'), 'utf8').trim());
  await adm.click('#signIn');
  await adm.waitForSelector('#reports button');
  check((await adm.textContent('#reports')).includes('abuse: test report'), 'admin sees the open report');
  await adm.click('#reports button');
  await adm.waitForSelector('#playerCard', { state: 'visible' });
  await adm.fill('#banReason', 'abusive messages (test)');
  await adm.click('#ban24');
  await b.waitForSelector('#bannedCard', { state: 'visible', timeout: 5000 });
  check((await b.textContent('#bannedText')).includes('abusive messages'), 'banned player sees the ban and the reason');
  if (shots) await adm.screenshot({ path: path.join(shots, 'online-admin.png'), fullPage: true });
  await b.reload();
  await b.waitForSelector('#bannedCard', { state: 'visible', timeout: 5000 });
  check(await b.evaluate(() => !!window.CUOnlineClient.id), 'a temporary ban keeps the account (not deleted from the browser)');
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
