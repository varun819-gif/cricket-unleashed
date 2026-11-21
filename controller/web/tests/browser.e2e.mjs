// Browser end-to-end test: real Chromium emulating an Android phone (Pixel 7)
// uses the real controller page against the real cricket_controller_host.
// This is phone EMULATION in desktop Chromium, not a physical Android device.
//
// Run: node controller/web/tests/browser.e2e.mjs [screenshotDir]
// Optional QR scan check: set CU_JSQR_DIR to a folder with jsqr + pngjs installed.
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

const host = spawn(hostBin, ['--port', '0', '--bind', '127.0.0.1', '--host-ip', '127.0.0.1', '--print-links', '--quiet',
  '--humans', '1', '--dev-format', 'overs=2', '--seed', '11']);
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
  throw new Error('no link');
};

const browser = await chromium.launch();
try {
  const link = await linkFor('captain', 0);
  const port = new URL(link).port;
  const phone = await browser.newContext({ ...devices['Pixel 7'] });
  const page = await phone.newPage();
  const consoleErrors = [];
  page.on('console', (m) => { if (m.type() === 'error') consoleErrors.push(m.text()); });
  page.on('pageerror', (e) => consoleErrors.push(String(e)));

  await page.goto(link);
  await page.waitForFunction(() => window.CUApp && window.CUApp.client.status === 'connected', null, { timeout: 10000 });
  check(/Android/.test(await page.evaluate(() => navigator.userAgent)), 'runs as an Android Chrome user agent');
  check(!(await page.evaluate(() => location.hash)), 'pairing secret removed from the address bar');
  check((await page.textContent('#who')).includes('captain'), 'header shows team and role');

  // Play by tapping whatever the screen offers, like a person would.
  const tapIf = async (selector) => {
    const el = page.locator(selector).first();
    if (await el.count() && await el.isVisible() && await el.isEnabled()) { await el.tap(); return true; }
    return false;
  };
  let bowled = false, batted = false, shotBowl = false, shotBat = false;
  const deadline = Date.now() + 60000;
  while (Date.now() < deadline) {
    const st = await page.evaluate(() => window.CUApp.state);
    if (st && st.result) break;
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
      if (shots && !shotBowl) { await page.screenshot({ path: path.join(shots, 'phone-bowling.png') }); shotBowl = true; }
      await page.locator('.pitch').tap({ position: { x: 150, y: 90 } });
      bowled = (await tapIf('button:text("BOWL")')) || bowled;
    } else if (c.includes('bat')) {
      if (shots && !shotBat) { await page.screenshot({ path: path.join(shots, 'phone-batting.png') }); shotBat = true; }
      await sleep(900);  // wait for the ball to arrive, then play
      batted = (await tapIf('button:text("drive")')) || batted;
    }
    await sleep(120);
    const s2 = await page.evaluate(() => window.CUApp.state);
    if (bowled && batted && s2 && s2.score && s2.score.batting === 1 && s2.score.overs >= 1) break;
  }
  const st = await page.evaluate(() => window.CUApp.state);
  check(!!(st && st.score), 'scoreboard received');
  check(bowled, 'bowled a delivery from the phone (tap pitch + BOWL)');
  check(batted, 'played a shot from the phone');
  check((await page.textContent('#total')).length > 0, 'score shown on screen');

  // Wi-Fi blip: drop the socket. The phone reconnects by itself.
  const dev = await page.evaluate(() => window.CUApp.client.dev);
  await page.evaluate(() => window.CUApp.client.ws.close());
  await page.waitForFunction(() => window.CUApp.client.status === 'reconnecting', null, { timeout: 5000 });
  check((await page.textContent('#who')).includes('Reconnecting'), 'shows "Reconnecting" to the player');
  await page.waitForFunction(() => window.CUApp.client.status === 'connected', null, { timeout: 15000 });
  check(true, 'reconnected automatically');
  // Browser closed or page reloaded: the saved device key resumes without a QR scan.
  await page.reload();
  await page.waitForFunction(() => window.CUApp && window.CUApp.client.status === 'connected', null, { timeout: 15000 });
  check((await page.evaluate(() => window.CUApp.client.dev)) === dev, 'page reload resumes the same paired phone');
  if (shots) await page.screenshot({ path: path.join(shots, 'phone-after-reload.png') });
  check(consoleErrors.length === 0, 'no JavaScript errors' + (consoleErrors.length ? ': ' + consoleErrors.join(' | ') : ''));

  // A phone opening the page without a QR code gets the instructions.
  const fresh = await browser.newContext({ ...devices['Pixel 7'] });
  const p2 = await fresh.newPage();
  await p2.goto('http://127.0.0.1:' + port + '/');
  await p2.waitForSelector('#help:not([hidden])');
  check((await p2.textContent('#help')).includes('same Wi-Fi'), 'unpaired phone sees connection instructions');
  if (shots) await p2.screenshot({ path: path.join(shots, 'phone-instructions.png') });

  // The TV page (opened on the TV/PC itself) shows scannable codes.
  const tv = await browser.newContext({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: shots ? 2 : 1 });
  const tvPage = await tv.newPage();
  await tvPage.goto('http://127.0.0.1:' + port + '/tv');
  const svgs = await tvPage.locator('.code svg').count();
  check(svgs >= 2, 'TV page shows QR codes (' + svgs + ')');
  if (shots) await tvPage.screenshot({ path: path.join(shots, 'tv-page.png'), fullPage: true });
  if (process.env.CU_JSQR_DIR) {
    const req = createRequire(path.join(process.env.CU_JSQR_DIR, 'x.js'));
    const jsQR = req('jsqr');
    const { PNG } = req('pngjs');
    const png = PNG.sync.read(await tvPage.locator('.code svg').first().screenshot());
    const decoded = jsQR(new Uint8ClampedArray(png.data), png.width, png.height);
    const expected = await tvPage.locator('.code .link').first().textContent();
    check(decoded && decoded.data === expected, 'QR code on the TV decodes to its pairing link');
  }
} finally {
  await browser.close();
  host.kill();
}
console.log(failures ? failures + ' FAILED' : 'ALL PASSED');
process.exit(failures ? 1 : 0);
