/* Cricket Unleashed - phone controller screens.
 * Shows only the controls the TV says this phone may use right now
 * (state.controls), so the phone can never offer an illegal move.
 * All text from the TV is inserted with textContent (never as HTML). */
(function () {
  'use strict';
  const P = window.CUProtocol;
  const $ = (id) => document.getElementById(id);
  const el = (tag, props, ...kids) => {
    const n = document.createElement(tag);
    Object.entries(props || {}).forEach(([k, v]) => {
      if (k === 'class') n.className = v;
      else if (k === 'text') n.textContent = v;
      else if (k.startsWith('on')) n.addEventListener(k.slice(2), v);
      else n.setAttribute(k, v);
    });
    kids.flat().forEach((c) => c != null && n.append(c.nodeType ? c : document.createTextNode(String(c))));
    return n;
  };
  let touched = false;  // browsers only allow vibration after the first tap
  window.addEventListener('pointerdown', () => { touched = true; }, { once: true, capture: true });
  const buzz = (ms) => { if (!touched) return; try { navigator.vibrate && navigator.vibrate(ms); } catch (e) { /* ignore */ } };
  let toastTimer = null;
  function toast(msg) {
    const t = $('toast');
    t.textContent = msg;
    t.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.remove('show'), 2200);
  }

  let state = null;
  let ball = null;                 // {arriveAt, variation}
  const bowlChoice = { variation: 'stock', line: 0, length: 0.5, effort: 75 };
  let batDir = 0;
  let openerPicks = [];
  let lastSignature = '';
  let lastControls = '';

  const storage = (() => {
    try { localStorage.setItem('cu.test', '1'); localStorage.removeItem('cu.test'); return localStorage; } catch (e) {
      const m = new Map();
      return { getItem: (k) => (m.has(k) ? m.get(k) : null), setItem: (k, v) => m.set(k, v), removeItem: (k) => m.delete(k) };
    }
  })();

  const client = new P.ControllerClient({
    url: (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws',
    storage,
    WebSocket: window.WebSocket,
    name: (navigator.userAgentData && navigator.userAgentData.platform) || (/Android/.test(navigator.userAgent) ? 'Android phone' : 'Phone'),
    onEvent: onEvent,
  });

  function onEvent(type, data) {
    if (type === 'status') return showStatus(data);
    if (type === 'state') {
      const first = !state;
      state = data;
      if (first) showStatus({ status: client.status });  // header can now show the team name
      render();
      return;
    }
    if (type === 'ball') {
      ball = { arriveAt: performance.now() + data.arriveInMs, variation: data.variation, flight: data.arriveInMs };
      buzz(40);
      render();
      return;
    }
    if (type === 'ack' && data.ok === false) { toast(data.err || 'Not allowed'); buzz([30, 40, 30]); }
    if (type === 'error') toast(data);
  }

  function showStatus(s) {
    const dot = $('conn');
    dot.className = 'dot' + (s.status === 'connected' ? ' ok' : (s.status === 'reconnecting' || s.status === 'pairing') ? ' wait' : '');
    const side = client.side, role = client.role;
    const teamName = state && state.teams && side >= 0 ? state.teams[side] : (side >= 0 ? 'Team ' + (side + 1) : '');
    const label = {
      connected: (role === 'spectator' ? 'Watching' : (teamName + ' · ' + role)),
      reconnecting: 'Reconnecting… (the TV is keeping your place)',
      pairing: 'Pairing with the TV…',
      unpaired: 'Not paired',
      failed: 'Could not connect',
      revoked: 'Unpaired by the TV',
      stopped: 'Stopped',
    }[s.status] || s.status;
    $('who').textContent = label;
    const needHelp = s.status === 'unpaired' || s.status === 'failed' || s.status === 'revoked';
    $('help').hidden = !needHelp;
    $('helpError').textContent = s.status === 'failed' && s.detail ? 'Problem: ' + s.detail + '. Ask the TV for a new QR code.' : '';
    $('forget').hidden = !client.isPaired();
    if (s.status === 'connected') { $('score').hidden = false; render(); }
  }

  // ---------------------------------------------------------------- rendering
  function has(c) { return state && state.controls && state.controls.indexOf(c) >= 0; }
  function send(action) { client.act(action); buzz(15); }

  function renderScore() {
    if (!state) return;
    $('teams').textContent = (state.teams || []).join(' vs ');
    const s = state.score;
    if (!s) {
      $('total').textContent = state.toss === 'call' ? 'Toss' : 'Toss decision';
      $('chase').textContent = $('batters').textContent = $('bowler').textContent = '';
    } else {
      $('total').textContent = state.teams[s.batting] + ' ' + s.total + '/' + s.wickets + ' (' + s.overs + '.' + s.balls + ')';
      let chase = '';
      if (s.target) chase = 'Target ' + s.target + (s.rrr != null ? ' · need ' + s.rrr + ' an over' : '');
      if (s.day) chase += (chase ? ' · ' : '') + 'Day ' + s.day + ', session ' + s.session;
      $('chase').textContent = chase;
      $('chase').classList.toggle('free-hit', !!s.freeHit);
      if (s.freeHit) $('chase').textContent = (chase ? chase + ' · ' : '') + 'FREE HIT';
      const bat = (b, star) => (b && b.name ? b.name + (star ? '*' : '') + ' ' + b.runs + ' (' + b.balls + ')' : '');
      $('batters').textContent = [bat(state.striker, true), bat(state.nonStriker)].filter(Boolean).join('   ');
      $('bowler').textContent = state.bowler ? 'Bowling: ' + state.bowler.name + ' ' + state.bowler.overs + '.' + state.bowler.balls +
        '-' + state.bowler.runs + '-' + state.bowler.wickets : '';
    }
    $('message').textContent = state.result || state.message || '';
  }

  function card(title, ...kids) { return el('div', { class: 'card' }, title ? el('h2', { text: title }) : null, ...kids); }

  function countdown(ms) { return el('div', { class: 'countdown', text: Math.ceil(Math.max(0, ms) / 1000) + 's' }); }

  function render() {
    renderScore();
    const panel = $('panel');
    if (!state || client.status !== 'connected') { panel.replaceChildren(); return; }
    const signature = JSON.stringify([state.controls, state.options, state.waiting, state.stage, state.bowler && state.bowler.id,
      state.result, state.waitingForController, !!ball]);
    const urgent = state.reviewMsLeft != null || state.stageMsLeft != null;
    if (signature === lastSignature && !urgent) return;
    const controlsKey = JSON.stringify(state.controls);
    if (controlsKey !== lastControls && state.controls.length) buzz(25);  // your turn
    lastControls = controlsKey;
    lastSignature = signature;
    const parts = [];

    if (state.result) parts.push(card('Result', el('p', { text: state.result })));
    if (state.waitingForController >= 0 && state.waitingForController !== client.side)
      parts.push(card(null, el('p', { class: 'wait', text: 'Waiting for the other team’s phone to reconnect…' })));

    if (has('toss-call')) parts.push(card('Call the toss', el('div', { class: 'grid2' },
      el('button', { text: 'Heads', onclick: () => send({ do: 'toss-call', face: 'heads' }) }),
      el('button', { text: 'Tails', onclick: () => send({ do: 'toss-call', face: 'tails' }) }))));
    if (has('toss-decide')) parts.push(card('You won the toss!', el('div', { class: 'grid2' },
      el('button', { text: 'Bat first', onclick: () => send({ do: 'toss-decide', choice: 'bat' }) }),
      el('button', { text: 'Bowl first', onclick: () => send({ do: 'toss-decide', choice: 'bowl' }) }))));

    const opts = state.options || {};
    if (has('openers')) {
      const list = el('div', { class: 'list' });
      (opts.batters || []).forEach((b) => {
        const i = openerPicks.indexOf(b.id);
        list.append(el('button', { class: i >= 0 ? 'on' : '', onclick: () => {
          const k = openerPicks.indexOf(b.id);
          if (k >= 0) openerPicks.splice(k, 1); else if (openerPicks.length < 2) openerPicks.push(b.id);
          lastSignature = ''; render();
        } }, b.name, i === 0 ? el('small', { text: 'on strike' }) : i === 1 ? el('small', { text: 'non-striker' }) : null));
      });
      parts.push(card('Pick your openers (strike first)', list,
        el('button', { class: 'big', text: 'Send them in', onclick: () => {
          send({ do: 'openers', striker: openerPicks[0], nonStriker: openerPicks[1] }); openerPicks = [];
        } })));
      parts[parts.length - 1].querySelector('.big').disabled = openerPicks.length !== 2;
    }
    if (has('batter')) {
      const list = el('div', { class: 'list' });
      (opts.batters || []).forEach((b) => list.append(el('button', { text: b.name, onclick: () => send({ do: 'batter', player: b.id }) })));
      parts.push(card('Next batter', list));
    }
    if (has('bowler')) {
      const list = el('div', { class: 'list' });
      (opts.bowlers || []).forEach((b) => list.append(el('button', { onclick: () => send({ do: 'bowler', player: b.id }) },
        b.name, el('small', { text: b.kind + ' · ' + b.overs + ' ov' + (b.specialist ? '' : ' · part-time') }))));
      parts.push(card('Who bowls this over?', list));
    }
    if (has('review')) {
      parts.push(card('Review the decision?', state.reviewMsLeft >= 0 ? countdown(state.reviewMsLeft) : null,
        el('div', { class: 'grid2' },
          el('button', { text: 'REVIEW', onclick: () => send({ do: 'review' }) }),
          el('button', { class: 'secondary', text: 'Accept', onclick: () => send({ do: 'accept' }) }))));
    }
    if (has('follow-on')) parts.push(card('Enforce the follow-on?', el('div', { class: 'grid2' },
      el('button', { text: 'Enforce', onclick: () => send({ do: 'follow-on', enforce: true }) }),
      el('button', { class: 'secondary', text: 'Bat again', onclick: () => send({ do: 'follow-on', enforce: false }) }))));
    if (has('ready')) parts.push(card('Break in play', el('p', { text: state.message || '' }),
      el('button', { class: 'big', text: 'Ready to play', onclick: () => send({ do: 'ready' }) })));

    if (has('bowl')) parts.push(bowlCard());
    if (has('bat') || (ball && state.stage === 'ball' && client.side === state.score?.batting)) parts.push(batCard());
    if (has('fielding')) parts.push(card('Fielders', el('div', { class: 'grid3' },
      ['dive-left', 'catch', 'dive-right', 'throw-keeper', 'throw-bowler'].map((a) =>
        el('button', { class: 'chip', text: a.replace('-', ' '), onclick: () => send({ do: 'fielding', act: a }) })))));

    if (has('field') || has('new-ball') || has('declare')) {
      const sel = el('select', { 'aria-label': 'Field preset' }, (opts.fieldPresets || []).map((p) => el('option', { value: p, text: p })));
      const kids = [];
      if (has('field')) kids.push(sel, el('button', { class: 'secondary', text: 'Set field', onclick: () => send({ do: 'field', preset: sel.value }) }));
      if (has('new-ball')) kids.push(el('button', { class: 'secondary', text: 'Take the new ball', onclick: () => send({ do: 'new-ball' }) }));
      if (has('declare')) kids.push(el('button', { class: 'secondary', text: 'Declare', onclick: () => { if (confirm('Declare the innings?')) send({ do: 'declare' }); } }));
      parts.push(card('Captain', el('div', { class: 'list' }, kids)));
    }

    if (!parts.length) parts.push(card(null, el('p', { class: 'wait', text: waitingText() })));
    panel.replaceChildren(...parts);
  }

  function waitingText() {
    if (!state) return '';
    if (state.role === 'spectator') return 'Enjoy the match!';
    const mine = state.waitingSide === client.side;
    const w = { 'toss-call': 'the toss call', 'toss-decision': 'the toss decision', openers: 'the openers', batter: 'the next batter',
      bowler: 'the bowler', review: 'a review decision', 'follow-on': 'the follow-on decision', interval: 'both teams to be ready',
      delivery: state.stage === 'bowl' ? 'the bowler' : 'the ball', none: 'the next match' }[state.waiting] || state.waiting;
    return (mine ? 'Your teammate is choosing ' : 'Waiting for ') + w + '…';
  }

  function bowlCard() {
    const vars = (state.bowler && state.bowler.variations) || ['stock'];
    if (vars.indexOf(bowlChoice.variation) < 0) bowlChoice.variation = vars[0];
    const chips = el('div', { class: 'row' }, vars.map((v) => el('button', {
      class: 'chip' + (v === bowlChoice.variation ? ' on' : ''), text: v,
      onclick: () => { bowlChoice.variation = v; lastSignature = ''; render(); } })));
    const marker = el('div', { class: 'marker' });
    const pitch = el('div', { class: 'pitch', 'aria-label': 'Tap where the ball should pitch' },
      el('span', { class: 'zone zone-short', text: 'SHORT' }), el('span', { class: 'zone zone-good', text: 'GOOD' }),
      el('span', { class: 'zone zone-full', text: 'FULL' }), el('div', { class: 'stumps' }), marker);
    const place = () => {
      marker.style.left = (50 + bowlChoice.line * 40) + '%';
      marker.style.top = (8 + (1 - bowlChoice.length) * 84) + '%';
    };
    place();
    pitch.addEventListener('pointerdown', (e) => {
      const r = pitch.getBoundingClientRect();
      bowlChoice.line = Math.max(-1, Math.min(1, ((e.clientX - r.left) / r.width - 0.5) / 0.4));
      bowlChoice.length = Math.max(0, Math.min(1, 1 - ((e.clientY - r.top) / r.height - 0.08) / 0.84));
      place();
    });
    const effort = el('input', { type: 'range', min: '40', max: '100', value: String(bowlChoice.effort), 'aria-label': 'Effort' });
    effort.addEventListener('input', () => { bowlChoice.effort = Number(effort.value); });
    return card('Your delivery', state.stageMsLeft != null ? countdown(state.stageMsLeft) : null, chips, pitch,
      el('label', { text: 'Effort (more pace, less control)' }), effort,
      el('button', { class: 'big', text: 'BOWL', onclick: () => send({ do: 'bowl', variation: bowlChoice.variation,
        line: Number(bowlChoice.line.toFixed(2)), length: Number(bowlChoice.length.toFixed(2)), effort: bowlChoice.effort }) }));
  }

  function batCard() {
    const bar = el('div');
    const incoming = el('div', { class: 'incoming' }, bar);
    const arrow = el('div', { class: 'arrow' });
    const pad = el('div', { class: 'dirpad', 'aria-label': 'Drag to aim the shot' }, arrow,
      el('span', { class: 'lbl lbl-leg', text: 'LEG' }), el('span', { class: 'lbl lbl-off', text: 'OFF' }));
    const aim = (e) => {
      const r = pad.getBoundingClientRect();
      const dx = e.clientX - (r.left + r.width / 2), dy = (r.bottom) - e.clientY;
      batDir = Math.round(Math.atan2(dx, Math.max(1, dy)) * 180 / Math.PI);
      arrow.style.transform = 'rotate(' + batDir + 'deg)';
    };
    arrow.style.transform = 'rotate(' + batDir + 'deg)';
    pad.addEventListener('pointerdown', aim);
    pad.addEventListener('pointermove', (e) => { if (e.buttons) aim(e); });
    const shot = (s) => () => {
      const timing = ball ? Math.round(performance.now() - ball.arriveAt) : 0;
      send({ do: 'bat', shot: s, dir: batDir, timing });
      ball = null;
    };
    const animate = () => {
      if (!ball || !bar.isConnected) return;
      const p = 1 - (ball.arriveAt - performance.now()) / ball.flight;
      bar.style.width = Math.max(0, Math.min(1, p)) * 100 + '%';
      if (p < 1.6) requestAnimationFrame(animate);
    };
    requestAnimationFrame(animate);
    return card(ball ? 'Ball coming: ' + ball.variation + ' — tap as it arrives' : 'Get ready…', incoming, pad,
      el('div', { class: 'grid3' }, ['defend', 'drive', 'cut', 'pull', 'flick', 'sweep'].map((s) =>
        el('button', { class: s === 'defend' ? 'secondary' : '', text: s, onclick: shot(s) }))),
      el('div', { class: 'grid2' }, el('button', { text: 'LOFT', onclick: shot('loft') }),
        el('button', { class: 'ghost', text: 'Leave', onclick: shot('leave') })));
  }

  // ---------------------------------------------------------------- start
  $('fullscreen').addEventListener('click', () => {
    const d = document.documentElement;
    if (!document.fullscreenElement && d.requestFullscreen) d.requestFullscreen().catch(() => {});
    else if (document.exitFullscreen) document.exitFullscreen().catch(() => {});
  });
  $('forget').addEventListener('click', () => { client.stop(); client.forget(); showStatus({ status: 'unpaired' }); });
  setInterval(() => { if (client.status === 'connected') client.ping(); }, 5000);
  setInterval(() => { if (state && (state.reviewMsLeft != null || state.stageMsLeft != null)) render(); }, 250);

  const pairing = P.parsePairing(location.hash);
  if (pairing) history.replaceState(null, '', location.pathname);  // don't leave the secret in the address bar
  if (pairing || client.isPaired()) client.start(pairing); else showStatus({ status: 'unpaired' });
  // Phones pause pages in the background: reconnect quickly when shown again.
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden && client.status !== 'connected' && client.isPaired()) client.start(null);
  });
  window.CUApp = { client, get state() { return state; } };  // for automated tests
})();
