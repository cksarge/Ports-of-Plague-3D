// Records games played by the web version's own rules engine, so the Unreal
// rewrite can be checked against it move by move. It only reads the reference
// folder; the recordings go to Tools/golden/out/.
//   node Tools/golden/make_golden.mjs            record everything
//   node Tools/golden/make_golden.mjs --dump 12 40   print game 12's state after step 40
import { writeFileSync, mkdirSync } from 'node:fs';
import { createHash } from 'node:crypto';

const REF = '../../ports_web_reference_READ_ONLY/src/';
const { DATA, HOME_CITIES } = await import(REF + 'data.js');
const E = await import(REF + 'engine/index.js');
const { botDecide, playTurn, STRATEGIES } = await import(REF + 'engine/bots.js');
const { playBotGame } = await import(REF + 'engine/sim.js');

const sha = (text) => createHash('sha1').update(text, 'utf8').digest('hex');
const GAMES = 240;
const WHOLE = 120;
const NAMES = ['Ada', 'Bo', '  Cy ', 'D’Este & Sons', 'Ærø "the Bold"', 'Fugger\\Welser'];

// The setup of recorded game number i: every mix of houses, modes and options.
function setupFor(i) {
  const n = 2 + (i % 5);
  const players = [];
  for (let j = 0; j < n; j++) {
    const p = { name: NAMES[j], home: HOME_CITIES[(i + j) % HOME_CITIES.length] };
    const kind = (i + j) % 4;
    if (kind === 0) p.strategy = STRATEGIES[(i + j) % STRATEGIES.length];
    else if (kind === 1) { p.bot = true; p.skill = ['easy', 'medium', 'hard'][(i + j) % 3]; }
    else if (kind === 3) p.strategy = 'random';
    players.push(p);
  }
  if (players.every((p) => p.bot)) { delete players[0].bot; delete players[0].skill; }
  return {
    players,
    seed: `golden-${i}`,
    mode: i % 2 ? 'quick' : 'standard',
    difficulty: ['apprentice', 'chronicler', 'mortality'][i % 3],
    prePlague: i % 4 !== 0,
    timer: i % 7 === 0,
  };
}

// Everything a house could ask the rules about at this moment: whether each
// possible action is allowed (and the exact refusal if not), what each
// shipment would earn, and the scores. Uses no dice.
function battery(state) {
  const p = E.currentPlayer(state);
  const out = [];
  const check = (a) => out.push(E.checkAction(state, a));
  for (const from of p.posts) {
    for (const r of E.routesFrom(from)) {
      for (const offshore of [false, true]) {
        check({ type: 'ship', from, route: r.id, offshore });
        const q = E.shipQuote(state, p, r.id, from, { offshore });
        out.push([q.to, q.parts, q.fixed, q.min, q.max, q.contagionRisk, q.safe, q.fee, q.partner?.id ?? null]);
      }
    }
  }
  for (const c of DATA.cities) {
    for (const type of ['post', 'prepare', 'physician', 'marry', 'land', 'gates']) check({ type, city: c.id });
    out.push(E.isThreatened(state, c.id));
  }
  const places = [E.ESTATE, ...p.posts];
  for (const from of places) for (const to of places) for (const count of [1, 2, 3]) check({ type: 'move', from, to, count });
  for (const kind of ['hospital', 'confraternity', 'church', 'nonsense']) check({ type: 'charity', kind });
  check({ type: 'loan' });
  for (let partner = 0; partner <= state.players.length; partner++) check({ type: 'deal', partner });
  check({ type: 'dance' });
  check({ type: 'ship', from: p.home, route: 'nope' });
  check({ type: 'ship', from: 'moscow', route: 'novgorod-moscow' });
  out.push(E.charityCost(state, p), E.cost(state, 'openPost', p), E.actionPointsFor(state, p), E.lastPlaceId(state), E.roundNumber(state), E.totalRounds(state));
  const info = E.roundInfo(state);
  out.push([info.round, info.pre, info.label, info.months, info.headline, info.factIds]);
  for (const h of state.players) out.push(E.scorePlayer(h));
  out.push(E.legalShipments(state, p), E.legalPosts(state, p));
  return sha(JSON.stringify(out));
}

// Plays game i, calling record(step) after every move.
function play(i, record) {
  const setup = setupFor(i);
  const state = E.createGame(setup);
  let lcg = (E.seedFrom(setup.seed) ^ 0x9e3779b9) >>> 0;
  const chance = (oneIn) => { lcg = (Math.imul(lcg, 1103515245) + 12345) >>> 0; return (lcg >>> 16) % oneIn === 0; };
  const everyMove = i % 6 === 0;
  const snap = (step) => record({ ...step, h: sha(JSON.stringify(state)) }, state);
  snap({ op: 'create' });
  for (let guard = 0; state.phase !== 'ended' && guard < 5000; guard++) {
    if (state.phase !== 'actions') { E.advance(state); snap({ op: 'advance' }); continue; }
    const p = E.currentPlayer(state);
    if (i % 3 === 0 && chance(9)) {
      const b = battery(state);
      E.timeUp(state);
      snap({ op: 'timeUp', b });
      continue;
    }
    // The same steps as playTurn in bots.js, one move at a time.
    for (let moves = 0; moves < 40; moves++) {
      const b = everyMove || moves === 0 ? battery(state) : undefined;
      const move = E.botMove(state);
      if (move.type === 'end') { snap({ op: 'botEnd', b }); break; }
      if (move.type === 'decide') {
        const r = botDecide(state, move.choice);
        snap({ op: 'decide', choice: move.choice, ok: r.ok, b });
      } else {
        const r = E.performAction(state, move.action);
        snap({ op: 'act', action: move.action, ok: r.ok, b });
        if (!r.ok) break;
      }
    }
    if (p.pending.length) throw new Error(`game ${i}: a turn ran out of moves with a card still waiting`);
    const r = E.endTurn(state);
    snap({ op: 'endTurn', next: r.next });
  }
  if (state.phase !== 'ended') throw new Error(`game ${i} did not finish`);
  return { setup, state };
}

if (process.argv[2] === '--dump') {
  const [game, step] = [Number(process.argv[3]), Number(process.argv[4])];
  let n = 0;
  try {
    play(game, (s, state) => { if (n++ === step) { process.stdout.write(JSON.stringify(state)); throw new Error('done'); } });
  } catch (e) { if (e.message !== 'done') throw e; }
} else {
  const out = new URL('./out/', import.meta.url);
  mkdirSync(out, { recursive: true });
  const games = [];
  let steps = 0;
  const seen = new Set();
  for (let i = 0; i < GAMES; i++) {
    const rec = [];
    const { setup, state } = play(i, (s) => rec.push(s));
    for (const e of state.log) seen.add(e.type === 'card' ? `card:${e.card}` : e.type === 'fortune' ? `fortune:${e.card}` : e.type === 'decision' ? `decision:${e.kind}:${e.choice}` : e.type);
    steps += rec.length;
    games.push({ setup, steps: rec });
  }
  writeFileSync(new URL('steps.json', out), JSON.stringify(games));

  // Whole games through the web version's own playBotGame, checked by their final state.
  const whole = [];
  for (let i = 0; i < WHOLE; i++) {
    const s = setupFor(i * 7 + 3);
    const setup = { players: s.players, seed: `whole-${i}`, mode: s.mode, difficulty: s.difficulty, prePlague: s.prePlague };
    const { state, turns } = playBotGame(setup);
    whole.push({ setup, turns, h: sha(JSON.stringify(state)), winner: state.winner });
  }
  writeFileSync(new URL('whole.json', out), JSON.stringify(whole));

  const cards = [...DATA.chronicle, ...DATA.deck].map((c) => `card:${c.id}`).concat(DATA.fortune.map((c) => `fortune:${c.id}`));
  const unseen = cards.filter((c) => !seen.has(c));
  console.log(`${GAMES} games, ${steps} recorded steps; ${WHOLE} whole games.`);
  console.log(`cards never drawn: ${unseen.length ? unseen.join(', ') : 'none'}`);
  console.log('log entry kinds covered:', [...seen].filter((k) => !k.startsWith('card:') && !k.startsWith('fortune:')).sort().join(', '));
}
