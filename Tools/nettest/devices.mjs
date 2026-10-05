// Checks multi-device play without real phones: this script is several players' devices at once.
// It joins the 3D game's room as the website's page does, plays whole turns with the
// website's own rules engine, and (with --shadow) replays every move on that engine to prove the
// 3D game's state stays exactly the same as the website's would be. With --abuse it also sends
// the requests a broken or dishonest device might, and checks each is refused in the website's words.
//
//   node Tools/nettest/devices.mjs --code=BCDF --phones=6 --policy=random --shadow --abuse
//
// --policy=bot     each house plays as the website's computer player would
// --policy=random  each house picks any legal action at random (reaches every kind of action)
// Nothing in the website's folder is changed; its files are only read.
import { SUPABASE_URL, SUPABASE_KEY } from '../../ports_web_reference_READ_ONLY/src/net/config.js';
import * as E from '../../ports_web_reference_READ_ONLY/src/engine/index.js';
import { DATA, HOME_CITIES } from '../../ports_web_reference_READ_ONLY/src/data.js';
import { makeClientId } from '../../ports_web_reference_READ_ONLY/src/net/protocol.js';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const arg = (name, fallback = null) => {
  const hit = process.argv.find((a) => a === `--${name}` || a.startsWith(`--${name}=`));
  return hit ? (hit.includes('=') ? hit.slice(hit.indexOf('=') + 1) : true) : fallback;
};
const CODE = String(arg('code', '')).toUpperCase();
const PHONES = Number(arg('phones', 2));
const POLICY = arg('policy', 'bot');
const SHADOW = !!arg('shadow');
const ABUSE = !!arg('abuse');
// The relay does not keep the order of a message's fields, and the rules depend on it in places (which city
// is dealt with first). So the shadow game starts from the 3D game's own saved file, which does keep it,
// after checking that it holds the same game the devices were sent.
const SAVE = arg('save', fileURLToPath(new URL('../../Saved/SaveGames/ports-of-plague-save.json', import.meta.url)));
const STALL_SECONDS = Number(arg('stall', 90));
// How long a story card is left on the big screen before a device presses Next (to photograph the cards).
const NEXT_DELAY = Number(arg('nextdelay', 120));
const NAMES = ['House of the Tablet', 'House of the Phone', 'House of the Laptop', 'House of the Reader', 'House of the Watch', 'House of the Screen'];

// One room's message pipe, speaking Supabase Realtime's channel messages directly (as transport.js does
// through its library): every message goes to every other device in the room.
function openTransport(code) {
  return new Promise((resolve, reject) => {
    const topic = `realtime:pop-room-${code}`;
    const ws = new WebSocket(`${SUPABASE_URL.replace(/^http/, 'ws').replace(/\/$/, '')}/realtime/v1/websocket?apikey=${SUPABASE_KEY}&vsn=1.0.0`);
    const handlers = [];
    let ref = 1;
    let beat = null;
    const timer = setTimeout(() => reject(new Error('timed out')), 12000);
    const frame = (event, payload, to = topic) => ws.send(JSON.stringify({ topic: to, event, payload, ref: String(++ref), join_ref: '1' }));
    ws.onopen = () => ws.send(JSON.stringify({ topic, event: 'phx_join', ref: '1', join_ref: '1',
      payload: { config: { broadcast: { ack: false, self: false }, presence: { key: '' }, postgres_changes: [], private: false } } }));
    ws.onerror = (e) => reject(new Error(e.message ?? 'connection error'));
    ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      if (m.event === 'phx_reply' && m.ref === '1' && m.topic === topic) {
        clearTimeout(timer);
        if (m.payload.status !== 'ok') { reject(new Error(JSON.stringify(m.payload))); return; }
        beat = setInterval(() => frame('heartbeat', {}, 'phoenix'), 25000);
        resolve({
          send: (msg) => frame('broadcast', { type: 'broadcast', event: 'm', payload: msg }),
          onMessage: (fn) => handlers.push(fn),
          close: () => { clearInterval(beat); ws.close(); },
        });
      } else if (m.event === 'broadcast' && m.payload?.event === 'm') handlers.forEach((h) => h(m.payload.payload));
    };
  });
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const clone = (x) => JSON.parse(JSON.stringify(x));
const canon = (x) => (Array.isArray(x) ? x.map(canon) : x && typeof x === 'object' ? Object.fromEntries(Object.keys(x).sort().filter((k) => x[k] !== undefined).map((k) => [k, canon(x[k])])) : x);
const same = (a, b) => JSON.stringify(canon(a)) === JSON.stringify(canon(b));
const stamp = () => new Date().toISOString().slice(11, 19);
const say = (...a) => console.log(stamp(), ...a);

let passed = 0;
let failed = 0;
const failures = [];
function check(name, ok, detail = '') {
  if (ok) { passed++; say(`  ok    ${name}`); }
  else { failed++; failures.push(name); say(`  FAIL  ${name}${detail ? `  →  ${detail}` : ''}`); }
}

// ---------- One device ----------
async function device(label) {
  const t = await openTransport(CODE);
  const d = { label, cid: makeClientId(), t, inbox: [], lobby: null, game: null, lastRid: 0, waiters: [] };
  t.onMessage((m) => {
    if (!m || typeof m !== 'object') return;
    if (m.to && m.to !== d.cid) return;
    // A big screen that has just (re)opened the room asks who is there: its count of states starts again.
    if (m.t === 'roll-call') { d.send({ t: 'hello' }); reopened(); return; }
    if (m.t === 'lobby') d.lobby = m;
    if (m.t === 'state') { d.game = m; onState(m); }
    if (m.t === 'toast' || m.t === 'reject' || m.t === 'closed') d.inbox.push(m);
    d.waiters = d.waiters.filter((w) => !w(m));
  });
  d.send = (m) => t.send({ ...m, from: d.cid });
  d.raw = (m) => t.send(m);
  d.request = (m) => { d.lastRid = Date.now() + Math.random(); d.lastRid = Math.floor(d.lastRid); d.send({ ...m, rid: d.lastRid }); };
  // The next message of this kind addressed to this device, or null when none comes in time.
  d.expect = (kind, ms = 4000) => new Promise((resolve) => {
    const at = d.inbox.findIndex((m) => m.t === kind);
    if (at >= 0) { resolve(d.inbox.splice(at, 1)[0]); return; }
    const timer = setTimeout(() => resolve(null), ms);
    d.waiters.push((m) => { if (m.t !== kind) return false; clearTimeout(timer); d.inbox = d.inbox.filter((x) => x !== m); resolve(m); return true; });
  });
  d.until = (test, ms = 6000) => new Promise((resolve) => {
    if (test()) { resolve(true); return; }
    const timer = setTimeout(() => resolve(false), ms);
    d.waiters.push(() => { if (!test()) return false; clearTimeout(timer); resolve(true); return true; });
  });
  d.ping = setInterval(() => d.send({ t: 'ping', rev: d.game?.rev ?? null }), 8000);
  d.send({ t: 'hello' });
  return d;
}

// ---------- The game, as the devices see it ----------
let devices = [];
let latest = null;      // the newest state message
let shadow = null;      // the website engine's copy of the game
let shadowChecks = 0;
let shadowTried = false;
let moves = 0;
const kinds = {};       // how often each kind of action was accepted
let lastProgress = Date.now();
let nextPressed = { id: null, at: 0 };
let finished = null;
const done = new Promise((r) => { finished = r; });
let abuseQueue = [];    // checks still to run during the game
let abusing = false;
let refusedDecision = null;
let extraChecksDone = { wait: false };

let reopenedAt = 0;
let reopenings = 0;
function reopened() {
  if (!latest || Date.now() - reopenedAt < 3000) return;
  reopenedAt = Date.now();
  reopenings++;
  say('the big screen reopened the room: taking up the game from what it sends next');
  // Requests it never played are forgotten, and the shadow game starts again from its saved file.
  for (const d of devices) d.lastRid = 0;
  latest = null;
  shadow = null;
  shadowTried = false;
  moves = moves; lastProgress = Date.now();
}

function onState(m) {
  if (latest && m.rev < latest.rev) return;
  const fresh = !latest || m.rev > latest.rev;
  latest = m;
  if (fresh) { lastProgress = Date.now(); queueMicrotask(step); }
}

const seatOf = (d) => latest.seats.findIndex((s) => s.cid === d.cid);
const deviceFor = (seat) => devices.find((d) => d.cid === latest.seats[seat]?.cid);
const tail = (log) => log.slice(-15);

function legalActions(state, p) {
  const cities = [...DATA.cities.map((c) => c.id), E.ESTATE];
  const out = [];
  const add = (a) => { if (!E.checkAction(state, a)) out.push(a); };
  for (const from of p.posts) for (const r of E.routesFrom(from)) { add({ type: 'ship', from, route: r.id }); add({ type: 'ship', from, route: r.id, offshore: true }); }
  for (const city of cities) for (const type of ['post', 'prepare', 'physician', 'marry', 'land', 'gates']) add({ type, city });
  for (const from of cities) for (const to of cities) for (let count = 1; count <= E.familyAt(p, from); count++) add({ type: 'move', from, to, count });
  for (const kind of Object.keys(E.CHARITY_KINDS)) add({ type: 'charity', kind });
  add({ type: 'loan' });
  for (const other of state.players) if (other.id !== p.id) add({ type: 'deal', partner: other.id });
  return out;
}

function chooseMove(state) {
  const p = E.currentPlayer(state);
  if (p.pending.length) {
    const d = p.pending[0];
    const key = `${state.round}:${state.turn}:${p.pending.length}:${d.kind}`;
    // A choice the game refused (cannot pay, say) is answered the cautious way the next time.
    if (refusedDecision === key) return { type: 'decide', choice: d.kind === 'wageLaw' ? 'pay' : false };
    refusedDecision = key;
    if (POLICY === 'random') return { type: 'decide', choice: d.kind === 'wageLaw' ? (Math.random() < 0.5 ? 'obey' : 'pay') : Math.random() < 0.5 };
    return E.botMove(clone(state));
  }
  refusedDecision = null;
  if (POLICY !== 'random') return E.botMove(clone(state));
  if (p.ap <= 0 && !Object.values(p.free).some(Boolean)) return { type: 'end' };
  const legal = legalActions(state, p);
  if (!legal.length || Math.random() < 0.08) return { type: 'end' };
  // A kind of action first, then one of that kind: rare actions are reached as often as common ones.
  const types = [...new Set(legal.map((a) => a.type))];
  const least = types.sort((a, b) => (kinds[a] ?? 0) - (kinds[b] ?? 0));
  const type = Math.random() < 0.6 ? least[0] : types[Math.floor(Math.random() * types.length)];
  const of = legal.filter((a) => a.type === type);
  return { type: 'act', action: of[Math.floor(Math.random() * of.length)] };
}

function compareShadow(where) {
  if (!SHADOW || !shadow) return true;
  const host = clone(latest.state);
  const mine = clone(shadow);
  const hostLog = host.log; const myLog = tail(mine.log).filter((e) => e.seq >= (hostLog[0]?.seq ?? 0));
  delete host.log; delete mine.log;
  shadowChecks++;
  if (same(host, mine) && same(hostLog, myLog)) return true;
  failed++;
  failures.push(`state differs from the website's engine (${where})`);
  say(`  FAIL  the 3D game's state differs from the website engine's ${where}`);
  const diff = (a, b, path) => {
    if (same(a, b)) return;
    if (a && b && typeof a === 'object' && typeof b === 'object') { for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) diff(a[k], b[k], `${path}.${k}`); return; }
    say(`        ${path}: 3D game ${JSON.stringify(a)?.slice(0, 160)} · website ${JSON.stringify(b)?.slice(0, 160)}`);
  };
  diff(host, mine, 'state');
  diff(hostLog, myLog, 'log');
  shadow = null; // one report is enough
  return false;
}

async function step() {
  if (!latest || abusing) return;
  const { state, view, seats } = latest;
  if (state.phase === 'ended') {
    if (shadow && SHADOW) compareShadow('at the end of the game');
    finished();
    return;
  }
  // A story card: any device may press Next. One presses; another does if the card is still there.
  if (view.next) {
    const now = Date.now();
    const p = E.currentPlayer(state);
    if (ABUSE && !extraChecksDone.wait && p && !seats[p.id].bot && state.phase === 'actions' && deviceFor(p.id)) {
      extraChecksDone.wait = true;
      abusing = true;
      const d = deviceFor(p.id);
      d.inbox = [];
      d.request({ t: 'act', action: { type: 'loan' } });
      const toast = await d.expect('toast');
      check('an action sent while a card is open is answered “Please wait…”', toast?.text === 'Please wait…', JSON.stringify(toast));
      abusing = false;
    }
    if (nextPressed.id !== view.next.id || now - nextPressed.at > 4000 + NEXT_DELAY) {
      nextPressed = { id: view.next.id, at: now };
      const d = devices[(view.next.id + 0) % devices.length];
      const id = view.next.id;
      setTimeout(() => d.send({ t: 'next', id }), NEXT_DELAY);
    }
    return;
  }
  if (view.busy || state.phase !== 'actions') return;
  const p = E.currentPlayer(state);
  if (!p || seats[p.id].bot) return;
  const d = deviceFor(p.id);
  if (!d) return;
  if (seats[p.id].rid < d.lastRid) return; // the last request is still being played
  if (SHADOW && !shadow && !shadowTried && seats.some((x) => x.bot)) { shadowTried = true; say('no shadow game: a bot plays on the big screen, and this script does not play its turns'); }
  if (SHADOW && !shadow && !shadowTried) {
    shadowTried = true;
    abusing = true; // nothing else is sent while the file is read
    let saved = null;
    for (let i = 0; i < 20 && !saved; i++) {
      try { const file = JSON.parse(readFileSync(SAVE, 'utf8')).state; if (same({ ...file, log: 0 }, { ...state, log: 0 })) saved = file; } catch { /* not written yet */ }
      if (!saved) await sleep(150);
    }
    abusing = false;
    if (latest.state !== state) { shadowTried = false; queueMicrotask(step); return; }
    if (!saved) { check('the saved file holds the game the devices were sent', false, SAVE); shadow = null; }
    else { shadow = saved; say(`shadow game started from the 3D game's saved state (round ${state.round}, ${p.name} to play)`); }
  }
  if (!compareShadow(`before ${p.name}'s move ${moves + 1} (round ${state.round})`)) { /* reported */ }
  if (ABUSE && abuseQueue.length) {
    abusing = true;
    try { await abuseQueue.shift()(d, p); } catch (err) { check('abuse check ran', false, String(err?.stack ?? err)); }
    abusing = false;
    lastProgress = Date.now();
    queueMicrotask(step);
    return;
  }
  const move = chooseMove(state);
  moves++;
  if (move.type === 'act') {
    d.request({ t: 'act', action: move.action });
    kinds[move.action.type] = (kinds[move.action.type] ?? 0) + 1;
    if (shadow) { const r = E.performAction(shadow, clone(move.action)); if (!r.ok) say(`  note: website engine refused ${JSON.stringify(move.action)}: ${r.reason}`); }
  } else if (move.type === 'decide') {
    d.request({ t: 'decide', choice: move.choice });
    if (shadow) E.decide(shadow, move.choice);
  } else {
    d.request({ t: 'end' });
    if (shadow) {
      const r = E.endTurn(shadow);
      if (r.ok) while (shadow.phase !== 'actions' && shadow.phase !== 'ended') E.advance(shadow);
    }
  }
}

// ---------- Requests a device should never send ----------
function buildAbuse(stranger) {
  // What the big screen must answer, word for word (protocol.js and game.js).
  const unchanged = async (name, d, send, wanted) => {
    const before = clone(latest.state);
    const rid = d.lastRid;
    d.inbox = [];
    send();
    const toast = wanted === null ? await d.expect('toast', 1500) : await d.expect('toast');
    await sleep(250);
    d.lastRid = rid; // a refused request is not one the game goes on to play
    const still = same({ ...before, log: 0 }, { ...clone(latest.state), log: 0 });
    if (wanted === null) check(`${name}: no answer, nothing changes`, toast === null && still, toast ? `answered ${JSON.stringify(toast.text)}` : 'the game changed');
    else if (wanted === true) check(`${name}: refused, nothing changes`, !!toast && still, toast ? `the game changed (answer: ${toast.text})` : 'no answer');
    else check(`${name}: “${wanted}”`, toast?.text === wanted && still, toast ? `answered ${JSON.stringify(toast.text)}${still ? '' : ' and the game changed'}` : 'no answer');
  };
  const q = [];
  q.push(async (d, p) => {
    say('— requests from devices that should not be acting —');
    const other = devices.find((x) => x !== d && seatOf(x) >= 0);
    await unchanged('a device that never joined sends an action', stranger, () => stranger.request({ t: 'act', action: { type: 'loan' } }), null);
    await unchanged('another house tries to end this house’s turn', other, () => other.request({ t: 'end' }), `It is ${p.name}'s turn.`);
    await unchanged('another house tries to act out of turn', other, () => other.request({ t: 'act', action: { type: 'loan' } }), `It is ${p.name}'s turn.`);
    await unchanged('another house tries to answer a card out of turn', other, () => other.request({ t: 'decide', choice: true }), `It is ${p.name}'s turn.`);
  });
  q.push(async (d, p) => {
    say('— malformed requests from the house whose turn it is —');
    await unchanged('an action of a kind that does not exist', d, () => d.request({ t: 'act', action: { type: 'fly' } }), 'Unknown action.');
    await unchanged('an action with nothing in it', d, () => d.request({ t: 'act' }), 'Unknown action.');
    await unchanged('an action that is a word, not an object', d, () => d.request({ t: 'act', action: 'ship' }), 'Unknown action.');
    if (!p.pending.length) await unchanged('an answer when no card is waiting', d, () => d.request({ t: 'decide', choice: true }), 'There is no decision waiting.');
    await unchanged('Next for a card that is not a number', d, () => d.request({ t: 'next', id: 'first' }), 'Unknown card.');
    await unchanged('Next for a card that is not open', d, () => d.request({ t: 'next', id: 987654 }), null);
    await unchanged('a message of an unknown kind', d, () => d.request({ t: 'win' }), null);
  });
  q.push(async (d, p) => {
    say('— actions with impossible details (each must be refused and change nothing) —');
    if (p.pending.length) { say('  (skipped: a card is waiting)'); return; }
    const home = p.home;
    const bad = [
      ['ship with no route', { type: 'ship' }],
      ['ship with numbers for names', { type: 'ship', from: 123, route: {} }],
      ['ship on a route that does not exist', { type: 'ship', from: home, route: 'no-such-route' }],
      ['ship from a city with no post', { type: 'ship', from: 'atlantis', route: E.routesFrom(home)[0]?.id }],
      ['open a post in a city that does not exist', { type: 'post', city: 'atlantis' }],
      ['open a post with no city', { type: 'post' }],
      ['move a negative number of family', { type: 'move', from: home, to: E.ESTATE, count: -3 }],
      ['move no family', { type: 'move', from: home, to: E.ESTATE, count: 0 }],
      ['move a billion family', { type: 'move', from: home, to: E.ESTATE, count: 1e9 }],
      ['move half a family member', { type: 'move', from: home, to: E.ESTATE, count: 0.5 }],
      ['move family to a city that does not exist', { type: 'move', from: home, to: 'atlantis', count: 1 }],
      ['move family from a city that does not exist', { type: 'move', from: 'atlantis', to: home, count: 1 }],
      ['charity of a kind that does not exist', { type: 'charity', kind: 'bribe' }],
      ['a partnership with a house that does not exist', { type: 'deal', partner: 99 }],
      ['a partnership with a negative house', { type: 'deal', partner: -1 }],
      ['a partnership with itself', { type: 'deal', partner: p.id }],
      ['close the gates of nowhere', { type: 'gates', city: null }],
      ['marry into a city that does not exist', { type: 'marry', city: 'atlantis' }],
      ['buy land in a city that does not exist', { type: 'land', city: ['genoa'] }],
      ['prepare a household nowhere', { type: 'prepare', city: 42 }],
      ['hire a physician nowhere', { type: 'physician', city: '' }],
    ];
    for (const [name, action] of bad) await unchanged(name, d, () => d.request({ t: 'act', action }), true);
  });
  q.push(async (d) => {
    say('— nonsense on the wire (the big screen must simply carry on) —');
    const before = clone(latest.state);
    const junk = ['hello', 42, null, [1, 2, 3], { t: 5 }, { t: 'act', from: { a: 1 }, action: { type: 'loan' } }, { from: d.cid }, { t: 'join', from: d.cid, name: { a: 1 }, home: [] },
      { t: 'ping', from: d.cid, rev: 'x'.repeat(20000) }, { t: 'decide', from: 7, choice: { yes: true } }, { t: 'leave' }, { t: 'next', from: 'nobody', id: 1 }];
    for (const piece of junk) {
      stranger.raw(piece);
      await sleep(60);
    }
    await sleep(1200);
    check('junk messages change nothing', same({ ...before, log: 0 }, { ...clone(latest.state), log: 0 }));
    d.inbox = [];
    d.send({ t: 'hello' });
    const rev = latest.rev;
    check('the big screen still answers afterwards', await d.until(() => latest.rev >= rev && d.game?.rev >= rev, 5000) && !!d.game);
  });
  q.push(async (d, p) => {
    say('— joining a game that has already started —');
    stranger.inbox = [];
    stranger.send({ t: 'join', name: 'House of Nobody', home: 'genoa' });
    const no = await stranger.expect('reject');
    check('a new house cannot join a started game', no?.reason === 'This game has already started. To rejoin, type your house name exactly as before.', JSON.stringify(no));
    // A player whose browser lost its place takes the house back by typing its name (in any capitals).
    const victim = devices.find((x) => x !== d && seatOf(x) >= 0);
    const seat = seatOf(victim);
    const name = latest.seats[seat].name;
    stranger.send({ t: 'join', name: name.toUpperCase() });
    const took = await stranger.until(() => latest.seats[seat]?.cid === stranger.cid, 5000);
    check(`a new device takes “${name}” back by its house name`, took);
    if (took) {
      await unchanged('the old device of that house can no longer act', victim, () => victim.request({ t: 'end' }), null);
      devices[devices.indexOf(victim)] = stranger;
      stranger.label = victim.label;
    }
  });
  q.push(async (d) => {
    say('— the same request sent twice —');
    if (E.currentPlayer(latest.state).pending.length) { say('  (skipped: a card is waiting)'); return; }
    const turn = `${latest.state.round}:${latest.state.turn}`;
    const order = latest.state.order;
    const expected = order[(latest.state.turn + 1) % order.length];
    d.lastRid = Date.now();
    d.inbox = [];
    d.send({ t: 'end', rid: d.lastRid });
    d.send({ t: 'end', rid: d.lastRid });
    moves++;
    if (shadow) { const r = E.endTurn(shadow); if (r.ok) while (shadow.phase !== 'actions' && shadow.phase !== 'ended') E.advance(shadow); }
    await d.until(() => `${latest.state.round}:${latest.state.turn}` !== turn && !latest.view.next && !latest.view.busy, 60000);
    // Cards between rounds are passed by the usual step; here only the turn count matters.
    const now = latest.state;
    const advancedOnce = now.phase !== 'actions' || now.round !== Number(turn.split(':')[0]) || now.order[now.turn] === expected;
    check('“End turn” sent twice ends only one turn', advancedOnce, `turn went from ${turn} to ${now.round}:${now.turn}`);
  });
  return q;
}

// ---------- Before the game: the lobby ----------
async function lobbyChecks(spare) {
  say('— the lobby —');
  const first = devices[0];
  const reason = async (name, d, join, wanted) => {
    d.inbox = [];
    d.send({ t: 'join', ...join });
    const r = await d.expect('reject');
    check(`${name}: “${wanted}”`, r?.reason === wanted, JSON.stringify(r));
  };
  const freeHome = () => HOME_CITIES.find((h) => !first.lobby.seats.some((s) => s.home === h));
  if (first.lobby.seats.length >= 6) {
    await reason('a seventh house', spare, { name: 'House of Seven', home: freeHome() ?? 'genoa' }, 'The game is full (6 houses).');
    // Make room for the other checks: the last house steps out and comes back afterwards.
    const last = devices.at(-1);
    last.send({ t: 'leave' });
    check('a house that leaves the lobby gives up its seat', await first.until(() => first.lobby.seats.length === 5));
    await reason('a house with no name', spare, { name: '   ', home: freeHome() }, 'Every house needs a name.');
    await reason('a house name of 25 letters', spare, { name: 'A'.repeat(25), home: freeHome() }, 'House names can be at most 24 letters.');
    await reason('a home city that does not exist', spare, { name: 'House of Atlantis', home: 'atlantis' }, 'Choose one of the home cities.');
    await reason('a home city already taken', spare, { name: 'House of Copies', home: first.lobby.seats[0].home }, `${first.lobby.seats[0].name} already has that home city. Choose another.`);
    spare.send({ t: 'act', action: { type: 'loan' } });
    spare.raw('junk');
    // A seated house changes its mind about its name.
    const seat = first.lobby.seats.findIndex((s) => s.cid === devices[1].cid);
    devices[1].send({ t: 'join', name: 'House of the Quill', home: first.lobby.seats[seat].home });
    check('a seated house can change its name', await first.until(() => first.lobby.seats[seat]?.name === 'House of the Quill'));
    devices[1].send({ t: 'join', name: NAMES[1], home: first.lobby.seats[seat].home });
    await first.until(() => first.lobby.seats[seat]?.name === NAMES[1]);
    last.send({ t: 'join', name: NAMES[devices.length - 1], home: freeHome() });
    check('the house that left can join again', await first.until(() => first.lobby.seats.length === 6));
  }
  check('the lobby tells devices the game’s options', first.lobby.options && typeof first.lobby.options.mode === 'string', JSON.stringify(first.lobby.options));
}

// ---------- Run ----------
async function main() {
  if (!/^[BCDFGHJKMNPQRSTVWXZ2-9]{4}$/.test(CODE)) { console.error('Give the room code: --code=XXXX'); process.exit(2); }
  say(`joining room ${CODE} with ${PHONES} devices (${POLICY}${SHADOW ? ', shadow game' : ''}${ABUSE ? ', bad requests' : ''})`);
  // The big screen may still be opening its room: keep asking until it answers.
  const first = await device('device 1');
  for (let i = 0; i < 60 && !first.lobby && !first.game; i++) { await sleep(1000); first.send({ t: 'hello' }); }
  if (!first.lobby) { console.error('No lobby answered on that code.'); process.exit(2); }
  devices = [first];
  for (let i = 1; i < PHONES; i++) devices.push(await device(`device ${i + 1}`));
  const spare = await device('stranger');
  for (const [i, d] of devices.entries()) {
    const home = HOME_CITIES.find((h) => !first.lobby.seats.some((s) => s.home === h));
    d.send({ t: 'join', name: NAMES[i], home });
    const ok = await first.until(() => first.lobby.seats.some((s) => s.cid === d.cid), 8000);
    if (!ok) { check(`${d.label} joined`, false); process.exit(1); }
  }
  check(`${PHONES} devices joined, each with its own home city`, first.lobby.seats.filter((s) => !s.bot).length === PHONES && new Set(first.lobby.seats.map((s) => s.home)).size === first.lobby.seats.length);
  if (ABUSE) { await lobbyChecks(spare); abuseQueue = buildAbuse(spare); }
  say('waiting for the big screen to start the game…');
  const watchdog = setInterval(() => {
    if (!latest || Date.now() - lastProgress < STALL_SECONDS * 1000) { if (latest && Date.now() - lastProgress > 6000) queueMicrotask(step); return; }
    const p = latest.state.phase === 'actions' ? E.currentPlayer(latest.state) : null;
    check('the game keeps moving', false, `nothing new for ${STALL_SECONDS}s: rev ${latest.rev}, phase ${latest.state.phase}, round ${latest.state.round}, turn ${latest.state.turn}, card ${JSON.stringify(latest.view.next?.title ?? null)}, busy ${latest.view.busy}, ${p ? `${p.name} to play, rid ${latest.seats[p.id].rid} vs sent ${deviceFor(p.id)?.lastRid}` : ''}`);
    finished();
  }, 3000);
  await done;
  clearInterval(watchdog);
  const s = latest.state;
  if (s.phase === 'ended') {
    say(`game over after ${moves} requests: ${s.finalScores.map((r) => `${s.players[r.id ?? r.player]?.name ?? '?'} ${r.total}`).join(', ')}`);
    check('the game was played to its end from the devices', true);
  }
  say(`actions accepted by kind: ${Object.entries(kinds).map(([k, n]) => `${k} ${n}`).join(', ') || 'none'}`);
  if (reopenings) check(`the game went on after the big screen reopened its room (${reopenings} time${reopenings > 1 ? 's' : ''})`, s.phase === 'ended');
  if (SHADOW && shadowChecks) check(`the 3D game matched the website's engine at all ${shadowChecks} checkpoints`, shadowChecks > 0 && !failures.some((f) => f.startsWith('state differs')));
  if (ABUSE && abuseQueue.length) check('every bad-request check had its turn', false, `${abuseQueue.length} left`);
  say(`${passed} passed, ${failed} failed${failed ? `: ${failures.join('; ')}` : ''}`);
  await sleep(500);
  process.exit(failed ? 1 : 0);
}

main().catch((err) => { console.error(err); process.exit(2); });
