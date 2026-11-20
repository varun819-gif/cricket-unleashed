/* Moderation page: review reports, ban / unban, reset names. Uses the same
 * WebSocket as the game with an admin token (docs/MODERATION.md). All text
 * from players is shown with textContent. */
(function () {
  'use strict';
  const $ = (id) => document.getElementById(id);
  const el = (tag, text, cls) => { const n = document.createElement(tag); if (text != null) n.textContent = text; if (cls) n.className = cls; return n; };
  let ws = null, seq = 0, waiting = new Map(), current = null, currentReport = '', tab = 'open';
  function toast(m) { const t = $('toast'); t.textContent = m; t.classList.add('show'); setTimeout(() => t.classList.remove('show'), 2500); }
  function req(k, data) {
    return new Promise((resolve) => {
      if (!ws) return resolve({ ok: false, err: 'not connected' });
      const s = ++seq; waiting.set(s, resolve);
      ws.send(JSON.stringify(Object.assign({}, data || {}, { k, s })));
    });
  }
  function when(ms) { return new Date(ms).toLocaleString(); }
  async function act(k, data, done) {
    const r = await req(k, data);
    toast(r.ok ? done : (r.err || 'failed'));
    if (r.ok) { await loadReports(); if (current) await openPlayer(current, currentReport); }
  }
  async function loadReports() {
    const r = await req('admin-reports', { status: tab === 'open' ? 'open' : '' });
    if (!r.ok) return;
    const list = r.reports.filter((x) => tab === 'open' || x.status !== 'open');
    if (!list.length) return $('reports').replaceChildren(el('p', 'Nothing here.'));
    const t = el('table');
    list.forEach((x) => {
      const tr = el('tr');
      const b = el('button', 'Review', 'ghost small');
      b.addEventListener('click', () => openPlayer(x.target.id, x.id));
      tr.append(el('td', when(x.created)), el('td', x.target.name + ' (' + x.target.reportsAgainst + ' reports)'),
        el('td', x.reason + (x.note ? ': ' + x.note : '')), el('td', 'by ' + x.reporter.name), el('td', x.status + (x.resolution ? ' - ' + x.resolution : '')));
      const td = el('td'); td.append(b); tr.append(td);
      t.append(tr);
    });
    $('reports').replaceChildren(t);
  }
  async function openPlayer(id, reportId) {
    const r = await req('admin-player', { id });
    if (!r.ok) return toast(r.err);
    current = id; currentReport = reportId || '';
    const p = r.player;
    $('playerCard').hidden = false;
    $('pName').textContent = p.name + ' (' + p.id + ')';
    $('pInfo').textContent = 'Country ' + (p.country || '-') + ' · rating ' + p.rating + ' · ' + p.matches + ' matches' +
      (p.ban ? ' · BANNED ' + (p.ban.permanent ? 'permanently' : 'until ' + when(p.ban.until)) + ' (' + p.ban.reason + ')' : '');
    const rt = el('table');
    r.reports.forEach((x) => { const tr = el('tr'); tr.append(el('td', when(x.created)), el('td', x.reason), el('td', x.note), el('td', x.status)); rt.append(tr); });
    $('pReports').replaceChildren(rt);
    const ht = el('table');
    r.history.forEach((m) => { const tr = el('tr'); tr.append(el('td', m.mode), el('td', m.s0.team + ' ' + m.s0.total + '/' + m.s0.wickets + ' v ' + m.s1.team + ' ' + m.s1.total + '/' + m.s1.wickets), el('td', m.outcome), el('td', m.summary)); ht.append(tr); });
    $('pHistory').replaceChildren(ht);
  }
  const reason = () => $('banReason').value.trim();
  $('ban24').addEventListener('click', () => act('admin-ban', { id: current, hours: 24, reason: reason(), report: currentReport }, 'Banned for 24 hours'));
  $('ban7d').addEventListener('click', () => act('admin-ban', { id: current, hours: 168, reason: reason(), report: currentReport }, 'Banned for 7 days'));
  $('banPerm').addEventListener('click', () => { if (window.confirm('Ban this player permanently?')) act('admin-ban', { id: current, hours: 0, reason: reason(), report: currentReport }, 'Banned permanently'); });
  $('unban').addEventListener('click', () => act('admin-unban', { id: current }, 'Ban lifted'));
  $('rename').addEventListener('click', () => act('admin-rename', { id: current, report: currentReport }, 'Name reset'));
  $('dismiss').addEventListener('click', () => { if (currentReport) act('admin-resolve', { report: currentReport, status: 'dismissed', note: reason() }, 'Report dismissed'); });
  $('tabOpen').addEventListener('click', () => { tab = 'open'; loadReports(); });
  $('tabDone').addEventListener('click', () => { tab = 'done'; loadReports(); });
  $('signIn').addEventListener('click', () => {
    const token = $('token').value.trim();
    $('token').value = '';
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
    seq = 0;
    ws.onopen = async () => {
      const r = await req('admin-login', { token });
      if (!r.ok) { $('loginError').textContent = 'Wrong token (5 wrong tries lock admin sign-in for 10 minutes).'; return; }
      $('loginCard').hidden = true; $('work').hidden = false;
      loadReports();
    };
    ws.onmessage = (e) => {
      let j; try { j = JSON.parse(e.data); } catch (x) { return; }
      if (j.k === 'res' && waiting.has(j.re)) { waiting.get(j.re)(j); waiting.delete(j.re); }
      if (j.k === 'sping') ws.send(JSON.stringify({ k: 'spong', t: j.t, s: ++seq }));
    };
    ws.onclose = () => { ws = null; if ($('work').hidden === false) toast('Disconnected - sign in again'); $('loginCard').hidden = false; $('work').hidden = true; };
  });
  window.CUAdmin = { req };  // for automated tests
})();
