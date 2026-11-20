/* Cricket Unleashed - phone controller protocol (client side).
 *
 * Mirrors controller/src/ControllerHub.cpp. Works in phone browsers and in
 * Node.js (for tests). Phone browsers disable WebCrypto on plain http://
 * local pages, so SHA-256 and HMAC are implemented here and checked against
 * the standard test vectors (controller/web/tests/protocol.test.mjs).
 * crypto.getRandomValues IS available on http pages and is used for nonces.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CUProtocol = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  // ---------------------------------------------------------------- bytes
  function utf8(str) { return new TextEncoder().encode(str); }
  function toHex(bytes) {
    let s = '';
    for (let i = 0; i < bytes.length; i++) s += (bytes[i] < 16 ? '0' : '') + bytes[i].toString(16);
    return s;
  }
  function concat(a, b) {
    const out = new Uint8Array(a.length + b.length);
    out.set(a, 0); out.set(b, a.length);
    return out;
  }
  function base64UrlDecode(text) {
    if (!/^[A-Za-z0-9_-]*$/.test(text) || text.length % 4 === 1) return null;
    const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
    const out = [];
    let acc = 0, bits = 0;
    for (const ch of text) {
      acc = (acc << 6) | alphabet.indexOf(ch);
      bits += 6;
      if (bits >= 8) { bits -= 8; out.push((acc >> bits) & 0xff); }
      acc &= (1 << bits) - 1;
    }
    return new Uint8Array(out);
  }
  function randomHex(nBytes) {
    const b = new Uint8Array(nBytes);
    crypto.getRandomValues(b);
    return toHex(b);
  }

  // ---------------------------------------------------------------- SHA-256
  const K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]);

  function sha256(data) {
    const bytes = typeof data === 'string' ? utf8(data) : data;
    const bitLen = bytes.length * 8;
    const padded = new Uint8Array(((bytes.length + 9 + 63) >> 6) << 6);
    padded.set(bytes);
    padded[bytes.length] = 0x80;
    const dv = new DataView(padded.buffer);
    dv.setUint32(padded.length - 8, Math.floor(bitLen / 0x100000000));
    dv.setUint32(padded.length - 4, bitLen >>> 0);
    const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
    const w = new Uint32Array(64);
    const rotr = (x, n) => (x >>> n) | (x << (32 - n));
    for (let off = 0; off < padded.length; off += 64) {
      for (let i = 0; i < 16; i++) w[i] = dv.getUint32(off + i * 4);
      for (let i = 16; i < 64; i++) {
        const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
        const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) >>> 0;
      }
      let [a, b, c, d, e, f, g, hh] = h;
      for (let i = 0; i < 64; i++) {
        const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const ch = (e & f) ^ (~e & g);
        const t1 = (hh + S1 + ch + K[i] + w[i]) >>> 0;
        const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const maj = (a & b) ^ (a & c) ^ (b & c);
        const t2 = (S0 + maj) >>> 0;
        hh = g; g = f; f = e; e = (d + t1) >>> 0; d = c; c = b; b = a; a = (t1 + t2) >>> 0;
      }
      h[0] = (h[0] + a) >>> 0; h[1] = (h[1] + b) >>> 0; h[2] = (h[2] + c) >>> 0; h[3] = (h[3] + d) >>> 0;
      h[4] = (h[4] + e) >>> 0; h[5] = (h[5] + f) >>> 0; h[6] = (h[6] + g) >>> 0; h[7] = (h[7] + hh) >>> 0;
    }
    const out = new Uint8Array(32);
    const odv = new DataView(out.buffer);
    for (let i = 0; i < 8; i++) odv.setUint32(i * 4, h[i]);
    return out;
  }

  function hmac(key, message) {
    let k = typeof key === 'string' ? utf8(key) : key;
    if (k.length > 64) k = sha256(k);
    const block = new Uint8Array(64);
    block.set(k);
    const ipad = block.map((x) => x ^ 0x36), opad = block.map((x) => x ^ 0x5c);
    const msg = typeof message === 'string' ? utf8(message) : message;
    return sha256(concat(opad, sha256(concat(ipad, msg))));
  }
  const hmacHex = (key, message) => toHex(hmac(key, message));
  function sameHex(a, b) {  // constant-time comparison
    if (typeof a !== 'string' || typeof b !== 'string' || a.length !== b.length) return false;
    let d = 0;
    for (let i = 0; i < a.length; i++) d |= a.charCodeAt(i) ^ b.charCodeAt(i);
    return d === 0;
  }

  // The QR link: http://<tv>:<port>/#p=<pairId>.<secret>
  function parsePairing(hash) {
    const m = /^#?p=([A-Za-z0-9_-]{12})\.([A-Za-z0-9_-]{43})$/.exec(hash || '');
    if (!m) return null;
    const secret = base64UrlDecode(m[2]);
    return secret && secret.length === 32 ? { pairId: m[1], secret } : null;
  }

  // ---------------------------------------------------------------- client
  // options: { url, storage (getItem/setItem/removeItem), WebSocket, name,
  //            now(): ms, setTimeout, clearTimeout, onEvent(type, data) }
  class ControllerClient {
    constructor(opts) {
      this.o = Object.assign({
        now: () => Date.now(),
        setTimeout: (f, ms) => setTimeout(f, ms),
        clearTimeout: (t) => clearTimeout(t),
        onEvent: () => {},
        name: 'Phone',
      }, opts);
      this.ws = null;
      this.status = 'idle';
      this.backoff = 500;
      this.timer = null;
      this.pending = new Map();   // actionId -> action (not yet acknowledged)
      this.clockOffset = 0;       // server time - phone time
      this.stopped = false;
      this.load();
    }
    load() {
      const s = this.o.storage;
      try {
        const saved = JSON.parse(s.getItem('cu.device') || 'null');
        if (saved && saved.dev && saved.key) {
          this.dev = saved.dev;
          this.devKey = base64UrlDecode(saved.key);
          this.side = saved.side;
          this.role = saved.role;
        }
        this.actionId = Number(s.getItem('cu.actionId') || '0');
      } catch (e) { this.actionId = 0; }
    }
    save() {
      const s = this.o.storage;
      const b64 = (bytes) => {
        let bin = '';
        bytes.forEach((x) => { bin += String.fromCharCode(x); });
        return btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
      };
      s.setItem('cu.device', JSON.stringify({ dev: this.dev, key: b64(this.devKey), side: this.side, role: this.role }));
      s.setItem('cu.actionId', String(this.actionId));
    }
    forget() {
      this.o.storage.removeItem('cu.device');
      this.dev = null; this.devKey = null;
    }
    isPaired() { return !!this.dev; }

    // Start with a QR pairing (from location.hash) or resume a saved device.
    start(pairing) {
      this.pairing = pairing || null;
      this.stopped = false;
      this.connect();
    }
    stop() {
      this.stopped = true;
      if (this.timer) this.o.clearTimeout(this.timer);
      if (this.ws) { try { this.sendSigned({ k: 'bye' }); this.ws.close(); } catch (e) { /* ignore */ } }
      this.setStatus('stopped');
    }
    setStatus(s, detail) {
      this.status = s;
      this.o.onEvent('status', { status: s, detail });
    }
    connect() {
      if (this.stopped) return;
      if (!this.pairing && !this.dev) { this.setStatus('unpaired'); return; }
      this.setStatus(this.dev && !this.pairing ? 'reconnecting' : 'pairing');
      const ws = new this.o.WebSocket(this.o.url);
      this.ws = ws;
      this.stage = 'hello';
      this.inSeq = 0; this.outSeq = 0;
      ws.onopen = () => {
        this.nc = randomHex(16);
        if (this.pairing) ws.send(JSON.stringify({ t: 'hello', pair: this.pairing.pairId, nc: this.nc }));
        else ws.send(JSON.stringify({ t: 'resume', dev: this.dev, nc: this.nc }));
      };
      ws.onmessage = (ev) => this.onMessage(typeof ev.data === 'string' ? ev.data : String(ev.data));
      ws.onclose = () => this.onClose(ws);
      ws.onerror = () => {};
    }
    onClose(ws) {
      if (ws !== this.ws) return;
      this.ws = null;
      this.session = null;
      if (this.stopped || this.status === 'revoked' || this.status === 'failed') return;
      // Reconnect with growing pauses (0.5 s ... 8 s) - the TV keeps our place.
      this.setStatus(this.dev ? 'reconnecting' : 'failed');
      if (!this.dev) return;
      this.timer = this.o.setTimeout(() => this.connect(), this.backoff);
      this.backoff = Math.min(this.backoff * 2, 8000);
    }
    fail(why) {
      this.setStatus('failed', why);
      if (this.ws) this.ws.close();
    }
    onMessage(text) {
      let j;
      try { j = JSON.parse(text); } catch (e) { return this.fail('bad message from TV'); }
      if (j.t === 'error') {
        // The TV no longer knows this phone (unpaired, or its list was reset):
        // stop retrying and ask for a fresh QR scan.
        if (/pair again|unknown device|unpaired/.test(j.why) || (!this.pairing && /proof failed/.test(j.why))) this.forget();
        this.o.onEvent('error', j.why);
        if (this.pairing || !this.dev) this.setStatus('failed', j.why);
        return;
      }
      if (j.t === 'revoked') { this.forget(); this.setStatus('revoked'); return; }
      if (j.t === 'replaced') return;
      if (this.stage === 'hello' && j.t === 'challenge') {
        if (!/^[0-9a-f]{32}$/.test(j.ns)) return this.fail('bad challenge');
        this.ns = j.ns;
        const mac = this.pairing
          ? hmacHex(this.pairing.secret, 'cu-pair|' + this.pairing.pairId + '|' + this.nc + '|' + this.ns)
          : hmacHex(this.devKey, 'cu-resume|' + this.dev + '|' + this.nc + '|' + this.ns);
        this.stage = 'prove';
        this.ws.send(JSON.stringify({ t: 'prove', mac, name: this.o.name }));
        return;
      }
      if (this.stage === 'prove' && j.t === 'welcome') {
        const devKey = this.pairing ? hmac(this.pairing.secret, 'cu-device|' + j.dev) : this.devKey;
        const session = hmac(devKey, 'cu-session|' + this.nc + '|' + this.ns);
        // Check the TV is genuine: only it knows the secret.
        if (!sameHex(j.mac, hmacHex(session, 'cu-welcome|' + j.dev))) return this.fail('TV could not prove itself');
        this.dev = j.dev; this.devKey = devKey; this.session = session;
        this.side = j.side; this.role = j.role;
        if (typeof j.last === 'number' && j.last > this.actionId) this.actionId = j.last;
        this.pairing = null;
        this.save();
        this.stage = 'ready';
        this.backoff = 500;
        this.setStatus('connected', { side: j.side, role: j.role, name: j.name });
        // Anything not acknowledged before the drop is sent again; the TV
        // ignores duplicates, so nothing happens twice.
        for (const [id, a] of this.pending) this.sendSigned({ k: 'action', id, a });
        this.ping();
        return;
      }
      if (this.stage === 'ready' && j.t === 'm') {
        if (!(j.s > this.inSeq) || !sameHex(j.mac, hmacHex(this.session, 's2c|' + j.s + '|' + j.b))) {
          return this.fail('message from TV failed its check');
        }
        this.inSeq = j.s;
        let body;
        try { body = JSON.parse(j.b); } catch (e) { return; }
        if (body.k === 'ack') {
          this.pending.delete(body.id);
          this.o.onEvent('ack', body);
        } else if (body.k === 'pong') {
          const rtt = this.o.now() - body.t;
          this.clockOffset = body.st - (body.t + rtt / 2);
          this.o.onEvent('pong', { rtt });
        } else {
          this.o.onEvent(body.k, body);
        }
      }
    }
    sendSigned(body) {
      if (!this.ws || this.stage !== 'ready') return false;
      const b = JSON.stringify(body);
      const s = ++this.outSeq;
      this.ws.send(JSON.stringify({ t: 'm', s, b, mac: hmacHex(this.session, 'c2s|' + s + '|' + b) }));
      return true;
    }
    // Sends a control. It is remembered until the TV acknowledges it.
    act(action) {
      const id = ++this.actionId;
      this.o.storage.setItem('cu.actionId', String(this.actionId));
      this.pending.set(id, action);
      this.sendSigned({ k: 'action', id, a: action });
      return id;
    }
    ping() { this.sendSigned({ k: 'ping', t: this.o.now() }); }
  }

  return { sha256, hmac, hmacHex, toHex, base64UrlDecode, parsePairing, sameHex, ControllerClient, randomHex };
});
