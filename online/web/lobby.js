/* Cricket Unleashed - online lobby: guest sign-in, quick / ranked matches,
 * private rooms, stats, history and leaderboards. The match itself is shown
 * by app.js (the same screens as the phone controller).
 * All text from the server is inserted with textContent (never as HTML). */
(function () {
  'use strict';
  const client = window.CUOnlineClient;
  const $ = (id) => document.getElementById(id);
  const el = (tag, text, cls) => { const n = document.createElement(tag); if (text != null) n.textContent = text; if (cls) n.className = cls; return n; };
  const store = (() => { try { localStorage.setItem('cu.t', '1'); localStorage.removeItem('cu.t'); return localStorage; } catch (e) { return null; } })();
  let room = null, queued = null, ended = null, tab = 'stats';
  let teamFlags = {};
  let reporting = null;  // {id, name, match}
  const countryFlag = { IN: 'IN', PK: 'PK', AU: 'AU', GB: 'ENG', ZA: 'ZA', NZ: 'NZ', LK: 'LK', BD: 'BD', AF: 'AF' };
  // A flag picture from this site only (codes come from a fixed list, never from other players).
  function flag(code) {
    if (!code || !/^[A-Z]{2,3}$/.test(code)) return null;
    const i = document.createElement('img');
    i.className = 'flag'; i.alt = ''; i.src = 'flags/' + code + '.svg';
    return i;
  }
  function withFlag(code, text) { const s = el('span'); const f = flag(code); if (f) s.append(f); s.append(text); return s; }
  function showTeamFlag() {
    [['team', 'teamFlag'], ['opponent', 'oppFlag']].forEach(([sel, img]) => {
      const code = teamFlags[$(sel).value];
      $(img).hidden = !code;
      if (code) $(img).src = 'flags/' + code + '.svg';
    });
  }

  function toast(msg) {
    const t = $('toast');
    t.textContent = msg;
    t.classList.add('show');
    setTimeout(() => t.classList.remove('show'), 2500);
  }
  async function req(kind, data) {
    const r = await client.request(kind, data);
    if (!r.ok) toast(r.err || 'Something went wrong');
    return r;
  }
  function choice() { return { team: $('team').value, format: $('format').value }; }

  function show() {
    const s = client.status;
    const lobby = s === 'lobby' || (s === 'reconnecting' && !client.inMatch && !!client.profile);
    $('lobby').hidden = !lobby;
    $('matchBar').hidden = !(client.inMatch && s === 'connected');
    $('playCard').hidden = !!(room || queued);
    $('soloCard').hidden = !!(room || queued);
    $('queueCard').hidden = !queued;
    $('roomCard').hidden = !room;
    $('endCard').hidden = !ended;
    if (s === 'failed') $('helpError').textContent = 'Problem: ' + ((client.lastDetail) || 'could not sign in') + '. You can create a new guest player.';
    if (s === 'unpaired' || s === 'failed') $('help').hidden = false;
    if (s === 'replaced' || s === 'banned') { $('help').hidden = true; $('lobby').hidden = true; }
    $('bannedCard').hidden = s !== 'banned';
    if (s === 'banned') {
      const b = client.ban || {};
      $('bannedText').textContent = (b.permanent ? 'This player is permanently banned.' : 'This player is banned until ' + new Date(b.until).toLocaleString() + '.') +
        (b.reason ? ' Reason: ' + b.reason : '');
    }
    $('reportCard').hidden = !reporting;
    if (reporting) $('reportTitle').textContent = 'Report ' + reporting.name;
    const p = client.profile;
    if (p) {
      const me = $('me');
      const h = el('h2'); h.append(withFlag(countryFlag[p.country], p.name));
      me.replaceChildren(h, el('p', 'Rating ' + p.rating + (p.country ? ' · ' + p.country : '') + ' · Server: ' + (p.region === 'in' ? 'India' : p.region)));
    }
    if (queued) $('queueText').textContent = 'Finding a ' + queued.mode + ' opponent (' + queued.format + ')…';
    if (room) {
      $('roomCode').textContent = room.code;
      const list = $('roomPlayers');
      const li = (x) => { const n = el('li'); n.append(x); return n; };
      list.replaceChildren(li(withFlag(teamFlags[room.host.team], room.host.name + ' - ' + room.host.team + ' (host)')),
        room.guest ? li(withFlag(teamFlags[room.guest.team], room.guest.name + ' - ' + room.guest.team)) : el('li', 'Waiting for your friend…'));
      $('startRoom').hidden = !(p && room.host.id === p.id);
      $('startRoom').disabled = !room.guest;
    }
    if (ended) {
      const r = ended.record;
      $('endText').textContent = r.summary || r.outcome;
      const d = ended.delta || 0;
      $('endRating').textContent = r.mode === 'solo' ? 'Single player vs computer - not counted in ratings or leaderboards' : r.mode === 'ranked' ? 'Rating ' + (ended.rating || '') + ' (' + (d >= 0 ? '+' : '') + d + ')' : 'Casual match - rating unchanged';
      $('endRating').className = d > 0 ? 'win' : d < 0 ? 'loss' : '';
    }
  }

  async function loadLists() {
    const r = await client.request('teams');
    if (!r.ok) return;
    const team = $('team'), format = $('format');
    const savedTeam = store && store.getItem('cu.team'), savedFormat = store && store.getItem('cu.format');
    team.replaceChildren(...r.teams.map((t) => { const o = el('option', t); o.value = t; return o; }));
    const names = { 'overs=2': '2 overs (quick)', 'overs=5': '5 overs', 'overs=10': '10 overs', t20: 'T20 (20 overs)', odi: 'ODI (50 overs)' };
    format.replaceChildren(...r.formats.map((f) => { const o = el('option', names[f] || f); o.value = f; return o; }));
    teamFlags = r.flags || {};
    const opp = $('opponent');
    opp.replaceChildren(...r.teams.map((t) => { const o = el('option', t); o.value = t; return o; }));
    const savedOpp = store && store.getItem('cu.opponent');
    opp.value = savedOpp && r.teams.indexOf(savedOpp) >= 0 ? savedOpp : 'Pakistan';
    if (savedTeam && r.teams.indexOf(savedTeam) >= 0) team.value = savedTeam;
    showTeamFlag();
    format.value = savedFormat && r.formats.indexOf(savedFormat) >= 0 ? savedFormat : r.formats[0];
    format.disabled = r.formats.length < 2;  // T20 only
  }

  async function showTab(which) {
    tab = which;
    $('boardFilters').hidden = which !== 'board';
    const body = $('tabBody');
    if (which === 'stats') {
      const r = await client.request('stats');
      if (!r.ok) return;
      const s = r.stats;
      const t = el('table');
      [['Matches', s.matches], ['Won / lost / tied', s.wins + ' / ' + s.losses + ' / ' + s.ties], ['Win %', s.winPct],
        ['Ranked matches', s.rankedMatches], ['Rating', s.rating], ['Runs (team)', s.runs], ['Wickets taken', s.wicketsTaken],
        ['Fours / sixes', s.fours + ' / ' + s.sixes], ['Best batter score', s.highestIndividual], ['Highest total', s.highestTeamTotal],
        ['Best bowling', s.bestBowling]].forEach(([k, v]) => { const tr = el('tr'); tr.append(el('th', k), el('td', String(v))); t.append(tr); });
      body.replaceChildren(t);
    } else if (which === 'history') {
      const r = await client.request('history', { limit: 20 });
      if (!r.ok) return;
      if (!r.matches.length) return body.replaceChildren(el('p', 'No matches yet.'));
      const t = el('table');
      r.matches.forEach((m) => {
        const mine = m.s0.account === client.profile.id ? 0 : 1;
        const res = m.winner === mine ? 'Won' : m.winner === 1 - mine ? 'Lost' : (m.outcome === 'tie' ? 'Tied' : 'No result');
        const tr = el('tr');
        tr.append(el('td', res, m.winner === mine ? 'win' : m.winner === 1 - mine ? 'loss' : ''),
          el('td', m.s0.team + ' ' + m.s0.total + '/' + m.s0.wickets + ' v ' + m.s1.team + ' ' + m.s1.total + '/' + m.s1.wickets),
          el('td', (m.mode === 'solo' ? 'vs computer' : m.mode) + (m.mode === 'ranked' ? ' (' + (mine ? m.d1 : m.d0) + ')' : '')));
        const opp = mine ? m.s0 : m.s1;
        if (opp.account) {
          const rb = el('button', 'Report', 'ghost small report');
          rb.addEventListener('click', () => { reporting = { id: opp.account, name: 'the ' + opp.team + ' player', match: m.id }; show(); });
          const td = el('td'); td.append(rb); tr.append(td);
        }
        t.append(tr);
      });
      body.replaceChildren(t);
    } else {
      const r = await client.request('leaderboard', { board: $('board').value, period: $('period').value, mode: $('bMode').value, country: $('bCountry').value });
      if (!r.ok) return;
      const rows = r.board.rows;
      if (!rows.length) return body.replaceChildren(el('p', 'Nobody on this leaderboard yet.'));
      const t = el('table');
      rows.forEach((x) => {
        const tr = el('tr'); const who = el('td'); who.append(withFlag(countryFlag[x.country], x.name));
        tr.append(el('td', '#' + x.rank), who, el('td', String(x.value))); t.append(tr);
      });
      body.replaceChildren(t);
    }
  }

  client.listen((type, data) => {
    if (type === 'status') {
      client.lastDetail = data.detail;
      if (data.status === 'lobby') { loadLists(); showTab(tab); }
      if (data.status === 'connected') { room = null; queued = null; ended = null; }
    }
    if (type === 'room') { room = data; queued = null; }
    if (type === 'room-closed') { room = null; toast('The host closed the room'); }
    if (type === 'queued') { queued = data; room = null; }
    if (type === 'end') { ended = data; room = null; queued = null; }
    if (type === 'notice') toast(data.text);
    if (type === 'replaced') toast('You signed in on another device or tab');
    show();
  });

  $('gStart').addEventListener('click', () => {
    $('helpError').textContent = '';
    client.createGuest($('gName').value.trim(), $('gCountry').value);
  });
  $('team').addEventListener('change', () => { if (store) store.setItem('cu.team', $('team').value); showTeamFlag(); });
  $('format').addEventListener('change', () => { if (store) store.setItem('cu.format', $('format').value); });
  $('opponent').addEventListener('change', () => { if (store) store.setItem('cu.opponent', $('opponent').value); showTeamFlag(); });
  $('solo').addEventListener('click', () => {
    if ($('opponent').value === $('team').value) return toast('Pick a different team for the computer');
    req('solo', { team: $('team').value, opponent: $('opponent').value, format: $('format').value, pitch: $('pitch').value });
  });
  $('quick').addEventListener('click', () => req('queue', Object.assign({ mode: 'casual' }, choice())));
  $('ranked').addEventListener('click', () => req('queue', Object.assign({ mode: 'ranked' }, choice())));
  $('cancelQueue').addEventListener('click', async () => { if ((await req('queue-cancel')).ok) { queued = null; show(); } });
  $('createRoom').addEventListener('click', () => req('room-create', choice()));
  $('joinRoom').addEventListener('click', () => req('room-join', { code: $('code').value, team: $('team').value }));
  $('startRoom').addEventListener('click', () => req('room-start'));
  $('leaveRoom').addEventListener('click', async () => { if ((await req('room-leave')).ok) { room = null; show(); } });
  $('endOk').addEventListener('click', () => { ended = null; show(); showTab(tab); });
  $('concede').addEventListener('click', () => { if (window.confirm('Concede this match? It counts as a loss.')) req('concede'); });
  $('reportCancel').addEventListener('click', () => { reporting = null; show(); });
  $('reportSend').addEventListener('click', async () => {
    if (!reporting) return;
    const r = await req('report', { id: reporting.id, reason: $('reportReason').value, note: $('reportNote').value, match: reporting.match });
    if (r.ok) { toast('Thanks - an admin will review it'); reporting = null; $('reportNote').value = ''; show(); }
  });
  $('tabStats').addEventListener('click', () => showTab('stats'));
  $('tabHistory').addEventListener('click', () => showTab('history'));
  $('tabBoard').addEventListener('click', () => showTab('board'));
  ['board', 'period', 'bMode', 'bCountry'].forEach((id) => $(id).addEventListener('change', () => showTab('board')));
  show();
  window.CULobby = { get room() { return room; }, get ended() { return ended; } };  // for automated tests
})();
