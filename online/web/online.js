/* Cricket Unleashed - online client (browser and Node).
 * Talks to the online game server over a WebSocket (wss:// on the internet).
 * The server decides everything; this client only sends requests and match
 * controls and shows what the server sends back.
 *   * Guest account: {id, token} kept in this browser's storage. The token
 *     is the only key to the account - there is no password.
 *   * Every message carries the next sequence number "s" (the server refuses
 *     repeats). Match controls carry an action id that survives reconnects,
 *     so a control re-sent after a drop is never applied twice.
 * It offers the same interface as the local ControllerClient, so the phone
 * controller screens (app.js) work unchanged for online matches. */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CUOnline = factory();
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  class OnlineClient {
    // opts: { url, storage, WebSocket, now(), setTimeout, clearTimeout, onEvent(type, data) }
    constructor(opts) {
      this.o = Object.assign({
        now: () => Date.now(),
        setTimeout: (f, ms) => setTimeout(f, ms),
        clearTimeout: (t) => clearTimeout(t),
        onEvent: () => {},
      }, opts);
      this.listeners = [this.o.onEvent];
      this.ws = null;
      this.status = 'idle';
      this.backoff = 500;
      this.timer = null;
      this.pending = new Map();    // match action id -> action (not yet acknowledged)
      this.requests = new Map();   // sequence number -> {resolve}
      this.inMatch = false;
      this.side = -1;
      this.role = 'captain';
      this.match = null;
      this.profile = null;
      this.clockOffset = 0;
      this.stopped = true;
      this.load();
    }
    listen(fn) { this.listeners.push(fn); }
    emit(type, data) { this.listeners.forEach((f) => { try { f(type, data); } catch (e) { /* a broken listener must not stop others */ } }); }
    load() {
      try {
        const a = JSON.parse(this.o.storage.getItem('cu.online') || 'null');
        if (a && a.id && a.token) { this.id = a.id; this.token = a.token; }
        this.actionId = Number(this.o.storage.getItem('cu.online.actionId') || '0') || 0;
      } catch (e) { this.actionId = 0; }
    }
    saveAccount() { this.o.storage.setItem('cu.online', JSON.stringify({ id: this.id, token: this.token })); }
    forget() {
      this.o.storage.removeItem('cu.online');
      this.id = null; this.token = null; this.profile = null;
    }
    isPaired() { return !!this.id; }   // "has an account" (same name as the local client)

    setStatus(s, detail) {
      this.status = s;
      this.emit('status', { status: s, detail });
    }
    // Log in with the saved account (or create the guest asked for).
    start() {
      this.stopped = false;
      if (this.ws) return;
      if (!this.id && !this.wantGuest) { this.setStatus('unpaired'); return; }
      this.connect();
    }
    createGuest(name, country) {
      this.wantGuest = { name: name || '', country: country || '' };
      this.start();
    }
    stop() {
      this.stopped = true;
      if (this.timer) this.o.clearTimeout(this.timer);
      if (this.ws) { try { this.ws.close(); } catch (e) { /* ignore */ } }
      this.ws = null;
      this.setStatus('stopped');
    }
    connect() {
      if (this.stopped) return;
      this.setStatus(this.id ? 'reconnecting' : 'joining');
      const ws = new this.o.WebSocket(this.o.url);
      this.ws = ws;
      this.seq = 0;
      this.ready = false;
      ws.onopen = () => {
        if (this.id) this.send({ k: 'login', id: this.id, token: this.token }, (r) => this.onLogin(r));
        else this.send({ k: 'guest', name: this.wantGuest.name, country: this.wantGuest.country }, (r) => this.onLogin(r));
      };
      ws.onmessage = (ev) => this.onMessage(typeof ev.data === 'string' ? ev.data : String(ev.data));
      ws.onclose = () => this.onClose(ws);
      ws.onerror = () => {};
    }
    onLogin(r) {
      if (!r.ok && r.banned) {
        // Banned: keep the account (a temporary ban ends), just stop.
        this.loginFailed = true;
        this.ban = r;
        this.setStatus('banned', r.permanent ? 'permanently banned' : 'banned until ' + new Date(r.until).toLocaleString());
        return;
      }
      if (!r.ok) {
        // Unknown account or wrong token: this browser's account is gone.
        if (this.id) this.forget();
        this.wantGuest = null;
        this.loginFailed = true;
        this.setStatus('failed', r.err);
        return;
      }
      if (r.token) { this.id = r.id; this.token = r.token; this.saveAccount(); }
      this.wantGuest = null;
      this.profile = { id: r.id, name: r.name, country: r.country, rating: r.rating, region: r.region };
      this.ready = true;
      this.backoff = 500;
      this.emit('profile', this.profile);
      if (!this.inMatch) this.setStatus('lobby');
    }
    onClose(ws) {
      if (ws !== this.ws) return;
      this.ws = null;
      this.ready = false;
      for (const [, r] of this.requests) r({ ok: false, err: 'connection lost' });
      this.requests.clear();
      if (this.stopped || this.status === 'replaced' || this.status === 'banned' || this.loginFailed) { this.loginFailed = false; return; }
      // Internet dropped: try again with growing pauses (0.5 s ... 8 s).
      // The server keeps our place in a match for 2 minutes.
      this.setStatus('reconnecting');
      if (!this.id) return;
      this.timer = this.o.setTimeout(() => this.connect(), this.backoff);
      this.backoff = Math.min(this.backoff * 2, 8000);
    }
    send(body, onReply) {
      if (!this.ws) return 0;
      const s = ++this.seq;
      if (onReply) this.requests.set(s, onReply);
      try { this.ws.send(JSON.stringify(Object.assign({}, body, { s }))); } catch (e) { return 0; }
      return s;
    }
    // A lobby request; resolves with the server's reply {ok, err, ...}.
    request(kind, data) {
      return new Promise((resolve) => {
        if (!this.ws || !this.ready) return resolve({ ok: false, err: 'not connected' });
        this.send(Object.assign({}, data || {}, { k: kind }), resolve);
      });
    }
    onMessage(text) {
      let j;
      try { j = JSON.parse(text); } catch (e) { return; }
      switch (j.k) {
        case 'res': {
          const r = this.requests.get(j.re);
          if (r) { this.requests.delete(j.re); r(j); }
          return;
        }
        case 'sping': this.send({ k: 'spong', t: j.t }); return;
        case 'pong': {
          const rtt = this.o.now() - j.t;
          this.clockOffset = j.st - (j.t + rtt / 2);
          this.emit('pong', { rtt });
          return;
        }
        case 'match':
          this.inMatch = true;
          this.match = j;
          this.side = j.side;
          this.role = j.role || 'captain';
          if (typeof j.last === 'number' && j.last > this.actionId) this.actionId = j.last;
          this.setStatus('connected', { side: j.side, role: this.role });
          this.emit('match', j);
          // Controls not yet acknowledged before a drop are sent again; the
          // server ignores any it already handled.
          for (const [id, a] of this.pending) this.send({ k: 'action', id, a });
          return;
        case 'ack':
          this.pending.delete(j.id);
          this.emit('ack', j);
          return;
        case 'end':
          this.inMatch = false;
          this.pending.clear();
          if (this.profile && typeof j.rating === 'number') this.profile.rating = j.rating;
          this.emit('end', j);
          this.setStatus('lobby');
          return;
        case 'replaced':
          this.setStatus('replaced');
          return;
        case 'banned':
          this.inMatch = false;
          this.ban = j;
          this.loginFailed = true;  // don't reconnect
          this.setStatus('banned', j.permanent ? 'permanently banned' : 'banned until ' + new Date(j.until).toLocaleString());
          return;
        case 'error':
          this.emit('error', j.why);
          return;
        default:
          this.emit(j.k, j);   // state, ball, notice, room, room-closed, queued
      }
    }
    // A match control (same as the local controller).
    act(action) {
      const id = ++this.actionId;
      this.o.storage.setItem('cu.online.actionId', String(this.actionId));
      this.pending.set(id, action);
      this.send({ k: 'action', id, a: action });
      return id;
    }
    ping() { this.send({ k: 'ping', t: this.o.now() }); }
  }

  // In the browser, hand app.js (the controller screens) this client.
  if (typeof window !== 'undefined') {
    window.CUMakeClient = (opts) => (window.CUOnlineClient = new OnlineClient(opts));
  }
  return { OnlineClient };
}));
