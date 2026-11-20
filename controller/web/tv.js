/* TV screen: refreshes the scoreboard and reloads when QR codes change. */
(function () {
  'use strict';
  let version = null;
  async function tick() {
    try {
      const r = await fetch('/tv.json', { cache: 'no-store' });
      const j = await r.json();
      if (version !== null && j.version !== version) return location.reload();
      version = j.version;
      document.getElementById('score').textContent = j.score || '';
      document.getElementById('msg').textContent = j.message || '';
    } catch (e) { /* game restarting */ }
  }
  setInterval(tick, 1000);
  tick();
})();
