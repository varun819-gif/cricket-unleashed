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
  let fieldBall = null;            // {arriveAt, flight, kind, fielder}: the ball on its way to our fielder
  let runBall = null;              // {arriveAt, flight}: ball in the field, our batters must call
  let stepOut = false;             // batter dances down the pitch for the next shot
  let showCard = false;            // scorecard open
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

  const clientOptions = {
    url: (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws',
    storage,
    WebSocket: window.WebSocket,
    name: (navigator.userAgentData && navigator.userAgentData.platform) || (/Android/.test(navigator.userAgent) ? 'Android phone' : 'Phone'),
    onEvent: onEvent,
  };
  // The online page supplies its own client (same interface, internet server).
  const client = window.CUMakeClient ? window.CUMakeClient(clientOptions) : new P.ControllerClient(clientOptions);

  function onEvent(type, data) {
    if (type === 'status') return showStatus(data);
    if (type === 'state') {
      const first = !state;
      state = data;
      state.receivedAt = performance.now();
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
    if (type === 'field') {
      fieldBall = { arriveAt: performance.now() + data.arriveInMs, flight: data.arriveInMs, kind: data.kind, fielder: data.fielder };
      buzz([20, 30, 20]);
      lastSignature = '';
      render();
      return;
    }
    if (type === 'run') {
      runBall = { arriveAt: performance.now() + data.arriveInMs, flight: data.arriveInMs };
      buzz([15, 25, 15]);
      lastSignature = '';
      render();
      return;
    }
    if (type === 'ack' && data.ok === false) { toast(data.err || 'Not allowed'); buzz([30, 40, 30]); }
    if (type === 'error') toast(data);
  }

  function showStatus(s) {
    const dot = $('conn');
    dot.className = 'dot' + ((s.status === 'connected' || s.status === 'lobby') ? ' ok' : (s.status === 'reconnecting' || s.status === 'pairing') ? ' wait' : '');
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
      lobby: 'Lobby',
      joining: 'Joining…',
      replaced: 'Signed in on another device',
      banned: 'Suspended',
    }[s.status] || s.status;
    $('who').textContent = label;
    const needHelp = s.status === 'unpaired' || s.status === 'failed' || s.status === 'revoked';
    $('help').hidden = !needHelp;
    $('helpError').textContent = s.status === 'failed' && s.detail ? 'Problem: ' + s.detail + '. Ask the TV for a new QR code.' : '';
    $('forget').hidden = !client.isPaired();
    if (s.status === 'connected') { $('score').hidden = false; render(); }
    if (s.status === 'lobby') { state = null; ball = null; $('score').hidden = true; render(); }
  }

  // ---------------------------------------------------------------- rendering
  function has(c) { return state && state.controls && state.controls.indexOf(c) >= 0; }
  function send(action) { client.act(action); buzz(15); }

  function renderScore() {
    if (!state) return;
    const cond = state.conditions;
    $('teams').textContent = (state.teams || []).join(' vs ') + (cond ? ' · ' + cond.pitch + ' pitch' +
      (cond.wear > 0.05 ? ' (wear ' + Math.round(cond.wear * 100) + '%)' : '') +
      (cond.windKph >= 1 ? ' · wind ' + cond.windKph + ' km/h' : '') + (cond.dew ? ' · dew' : '') : '');
    renderCard();
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
    // Last ball as the physics played it: speed, length, swing / turn, shot.
    $('message').textContent = (state.result || state.message || '') + (state.lastBall ? '\n' + state.lastBall.summary : '');
  }

  function card(title, ...kids) { return el('div', { class: 'card' }, title ? el('h2', { text: title }) : null, ...kids); }

  // Countdowns tick locally between server updates; only their text changes
  // (rebuilding the buttons every tick would swallow taps).
  function msLeft(key) {
    const v = state && state[key];
    return v == null ? null : v - (performance.now() - (state.receivedAt || performance.now()));
  }
  function countdownText(key) { return Math.ceil(Math.max(0, msLeft(key) || 0) / 1000) + 's'; }
  function countdown(key) { return el('div', { class: 'countdown', 'data-key': key, text: countdownText(key) }); }
  function tickCountdowns() {
    document.querySelectorAll('.countdown[data-key]').forEach((n) => { n.textContent = countdownText(n.getAttribute('data-key')); });
  }

  function render() {
    renderScore();
    const panel = $('panel');
    if (!state || client.status !== 'connected') { panel.replaceChildren(); return; }
    const signature = JSON.stringify([state.controls, state.options, state.waiting, state.stage, state.bowler && state.bowler.id,
      state.result, state.waitingForController, !!ball]);
    if (signature === lastSignature) { tickCountdowns(); return; }
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
      parts.push(card('Review the decision?', state.reviewMsLeft >= 0 ? countdown('reviewMsLeft') : null,
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
    if (has('fielding')) parts.push(fieldCard());
    if (has('run')) parts.push(runCard());

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
    return card('Your delivery', state.stageMsLeft != null ? countdown('stageMsLeft') : null, chips, pitch,
      el('label', { text: 'Effort (more pace, less control)' }), effort,
      el('button', { class: 'big', text: 'BOWL', onclick: () => send({ do: 'bowl', variation: bowlChoice.variation,
        line: Number(bowlChoice.line.toFixed(2)), length: Number(bowlChoice.length.toFixed(2)), effort: bowlChoice.effort }) }));
  }

  // The ball is on its way to one of our fielders: tap CATCH / DIVE as it
  // arrives (good timing makes it easier), and choose where to throw.
  function fieldCard() {
    const play = state.fieldPlay || {};
    const fb = fieldBall;
    const bar = el('div');
    const animate = () => {
      if (!fb || !bar.isConnected) return;
      const p = 1 - (fb.arriveAt - performance.now()) / fb.flight;
      bar.style.width = Math.max(0, Math.min(1, p)) * 100 + '%';
      if (p < 1.6) requestAnimationFrame(animate);
    };
    requestAnimationFrame(animate);
    const act = (a) => () => {
      const timing = fb ? Math.round(performance.now() - fb.arriveAt) : 0;
      send({ do: 'fielding', act: a, timing });
      if (a === 'catch' || a === 'dive') fieldBall = null;
    };
    const kind = play.kind || (fb && fb.kind) || 'stop';
    return card((kind === 'catch' ? 'Catch it! ' : 'Stop it! ') + (play.fielder || ''),
      el('div', { class: 'incoming' }, bar),
      el('button', { class: 'big', text: kind === 'catch' ? 'CATCH' : 'DIVE', onclick: act(kind === 'catch' ? 'catch' : 'dive') }),
      el('div', { class: 'grid2' },
        el('button', { class: 'chip', text: 'Throw: keeper end', onclick: act('throw-keeper') }),
        el('button', { class: 'chip', text: 'Throw: bowler end', onclick: act('throw-bowler') })));
  }

  // The ball is in the field: call the run. WAIT = stay in the crease;
  // 1 / 2 / 3 = run that many. No call in time = the batters judge it.
  function runCard() {
    const play = state.runPlay || {};
    const rb = runBall;
    const bar = el('div');
    const animate = () => {
      if (!rb || !bar.isConnected) return;
      const p = 1 - (rb.arriveAt - performance.now()) / rb.flight;
      bar.style.width = Math.max(0, Math.min(1, p)) * 100 + '%';
      if (p < 1.6) requestAnimationFrame(animate);
    };
    requestAnimationFrame(animate);
    const call = (c) => () => { send({ do: 'run', call: c }); runBall = null; };
    const where = (play.deep ? 'Deep, ' : 'In the ring, ') + (play.distance != null ? play.distance + ' m away' : '');
    const rs = play.runS || [];
    const back = play.backS != null ? 'Throw reaches the stumps in about ' + play.backS + ' s' : '';
    const times = rs.length === 3 ? 'Your batters: 1 run ' + rs[0] + ' s · 2 runs ' + rs[1] + ' s · 3 runs ' + rs[2] + ' s' : '';
    return card('Run? Ball going to ' + (play.fielder || 'a fielder'),
      el('p', { class: 'hint', text: where }),
      back ? el('p', { class: 'hint', text: back }) : null,
      times ? el('p', { class: 'hint', text: times + ' (a direct hit is faster!)' }) : null,
      el('div', { class: 'incoming' }, bar),
      el('div', { class: 'grid4' },
        el('button', { class: 'secondary', text: 'WAIT', onclick: call('wait') }),
        el('button', { text: '1', onclick: call('1') }),
        el('button', { text: '2', onclick: call('2') }),
        el('button', { text: '3', onclick: call('3') })),
      state.stageMsLeft != null ? countdown('stageMsLeft') : null);
  }

  // Full scorecard (batting, bowling, extras, fall of wickets, partnerships).
  function renderCard() {
    let box = $('cardview');
    if (!box) {
      box = el('section', { id: 'cardview', class: 'card', hidden: '' });
      $('score').after(box);
      const btn = el('button', { id: 'cardbtn', class: 'ghost small', text: 'Scorecard', onclick: () => { showCard = !showCard; renderCard(); } });
      $('score').append(btn);
    }
    box.hidden = !showCard;
    $('cardbtn').textContent = showCard ? 'Hide scorecard' : 'Scorecard';
    if (!showCard || !state || !state.scorecard) return;
    const table = (head, rows) => {
      const t = el('table', { class: 'sc' });
      const tr = el('tr'); head.forEach((h) => tr.append(el('th', { text: h }))); t.append(tr);
      rows.forEach((r) => { const x = el('tr'); r.forEach((c) => x.append(el('td', { text: String(c) }))); t.append(x); });
      return t;
    };
    const kids = [];
    state.scorecard.forEach((c) => {
      kids.push(el('h2', { text: c.team + ' ' + c.total + '/' + c.wickets + ' (' + c.overs + ' ov, RR ' + c.rr + ')' }));
      kids.push(table(['Batter', '', 'R', 'B', '4s', '6s', 'SR'], c.batting.map((b) => [b.name, b.how, b.r, b.b, b['4s'], b['6s'], b.sr])));
      const e = c.extras;
      kids.push(el('p', { text: 'Extras ' + e.total + ' (w ' + e.w + ', nb ' + e.nb + ', b ' + e.b + ', lb ' + e.lb + (e.pen ? ', pen ' + e.pen : '') + ')' }));
      kids.push(table(['Bowler', 'O', 'M', 'R', 'W', 'Econ'], c.bowling.map((b) => [b.name, b.o, b.m, b.r, b.w, b.econ])));
      if (c.fow.length) kids.push(el('p', { text: 'Fall of wickets: ' + c.fow.map((f) => f.n + '-' + f.score + ' (' + f.name + ', ' + f.over + ')').join(', ') }));
      if (c.partnerships.length) kids.push(el('p', { text: 'Partnerships: ' + c.partnerships.map((p) => p.runs + ' (' + p.balls + ')' + (p.unbroken ? '*' : '')).join(', ') }));
    });
    box.replaceChildren(...kids);
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
      send({ do: 'bat', shot: s, dir: batDir, timing, advance: stepOut && s !== 'leave' });
      ball = null;
      stepOut = false;
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
        el('button', { class: 'ghost', text: 'Leave', onclick: shot('leave') })),
      el('button', { class: 'chip' + (stepOut ? ' on' : ''), text: stepOut ? 'Stepping out ✓' : 'Step out (dance down the pitch)',
        onclick: (e) => { stepOut = !stepOut; e.currentTarget.classList.toggle('on', stepOut);
          e.currentTarget.textContent = stepOut ? 'Stepping out ✓' : 'Step out (dance down the pitch)'; } }));
  }

  // ---------------------------------------------------------------- start
  $('fullscreen').addEventListener('click', () => {
    const d = document.documentElement;
    if (!document.fullscreenElement && d.requestFullscreen) d.requestFullscreen().catch(() => {});
    else if (document.exitFullscreen) document.exitFullscreen().catch(() => {});
  });
  $('forget').addEventListener('click', () => { client.stop(); client.forget(); showStatus({ status: 'unpaired' }); });
  setInterval(() => { if (client.status === 'connected') client.ping(); }, 5000);
  setInterval(() => { if (state && (state.reviewMsLeft != null || state.stageMsLeft != null)) tickCountdowns(); }, 250);

  const pairing = P.parsePairing(location.hash);
  if (pairing) history.replaceState(null, '', location.pathname);  // don't leave the secret in the address bar
  if (pairing || client.isPaired()) client.start(pairing); else showStatus({ status: 'unpaired' });
  // Phones pause pages in the background: reconnect quickly when shown again.
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden && client.status !== 'connected' && client.isPaired()) client.start(null);
  });
  window.CUApp = { client, get state() { return state; }, ballMsLeft: () => (ball ? ball.arriveAt - performance.now() : null) };  // for automated tests
})();
