// Paints the board's surface textures (parchment and the wooden table) and
// saves them as PNG files in SourceArt/Textures. Everything is generated, so
// no outside artwork is needed. Run with: node Tools/make_textures.mjs
// Each texture repeats seamlessly, and comes with a normal map (_N) for relief.
import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync, crc32 } from 'node:zlib';

const OUT = new URL('../SourceArt/Textures/', import.meta.url);
mkdirSync(OUT, { recursive: true });
const N = 1024;

// A repeatable random number generator, so the textures come out the same every time.
function rng(seed) {
  let s = seed >>> 0;
  return () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let t = Math.imul(s ^ (s >>> 15), s | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// Smooth noise that wraps round after `px` by `py` cells.
function noise(px, py, seed) {
  const r = rng(seed);
  const grid = Float32Array.from({ length: px * py }, () => r());
  const fade = (t) => t * t * (3 - 2 * t);
  return (u, v) => {
    const x = u * px, y = v * py;
    const x0 = Math.floor(x), y0 = Math.floor(y);
    const fx = fade(x - x0), fy = fade(y - y0);
    const g = (i, j) => grid[(((j % py) + py) % py) * px + (((i % px) + px) % px)];
    return (g(x0, y0) * (1 - fx) + g(x0 + 1, y0) * fx) * (1 - fy) + (g(x0, y0 + 1) * (1 - fx) + g(x0 + 1, y0 + 1) * fx) * fy;
  };
}
// Several layers of noise, each finer and fainter. Result is about 0 to 1.
function layered(px, py, octaves, seed) {
  const layers = Array.from({ length: octaves }, (_, i) => noise(px << i, py << i, seed + i * 101));
  return (u, v) => {
    let sum = 0, weight = 0;
    layers.forEach((n, i) => { const w = 1 / (1 << i); sum += n(u, v) * w; weight += w; });
    return sum / weight;
  };
}

function png(name, pixels) {
  const raw = Buffer.alloc(N * (N * 3 + 1));
  for (let y = 0; y < N; y++) {
    raw[y * (N * 3 + 1)] = 0;
    for (let x = 0; x < N * 3; x++) raw[y * (N * 3 + 1) + 1 + x] = pixels[y * N * 3 + x];
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(body) >>> 0);
    return Buffer.concat([len, body, crc]);
  };
  const head = Buffer.alloc(13);
  head.writeUInt32BE(N, 0); head.writeUInt32BE(N, 4); head[8] = 8; head[9] = 2;
  writeFileSync(new URL(name, OUT), Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', head), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]));
  console.log('made', name);
}
const byte = (v) => Math.max(0, Math.min(255, Math.round(v * 255)));

// Turns a height field into a normal map.
function normals(height, strength) {
  const out = new Uint8Array(N * N * 3);
  const h = (x, y) => height[((y + N) % N) * N + ((x + N) % N)];
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const dx = (h(x + 1, y) - h(x - 1, y)) * strength, dy = (h(x, y + 1) - h(x, y - 1)) * strength;
    const l = Math.hypot(dx, dy, 1);
    const i = (y * N + x) * 3;
    out[i] = byte(0.5 - (dx / l) * 0.5); out[i + 1] = byte(0.5 - (dy / l) * 0.5); out[i + 2] = byte(0.5 + (1 / l) * 0.5);
  }
  return out;
}

// ---------- Parchment ----------
// A pale sheet with cloudy stains, fine grain, hair-like fibres and a few specks.
// It is used as a tint over the board's own colours, so it stays close to white.
{
  const r = rng(1347);
  const stains = layered(3, 3, 5, 11), clouds = layered(9, 9, 4, 23), grain = layered(96, 96, 3, 37);
  const marks = new Float32Array(N * N);
  const dab = (x, y, a) => { marks[(((Math.round(y) % N) + N) % N) * N + (((Math.round(x) % N) + N) % N)] += a; };
  for (let i = 0; i < 2600; i++) {
    let x = r() * N, y = r() * N, dir = r() * Math.PI * 2;
    const len = 14 + r() * 60, tone = (r() < 0.6 ? -1 : 1) * (0.25 + r() * 0.5);
    for (let s = 0; s < len; s++) {
      dab(x, y, tone); dab(x + 1, y, tone * 0.4); dab(x, y + 1, tone * 0.4);
      dir += (r() - 0.5) * 0.25; x += Math.cos(dir); y += Math.sin(dir);
    }
  }
  for (let i = 0; i < 500; i++) {
    const x = r() * N, y = r() * N, size = r() * 1.6, tone = -(0.6 + r() * 1.2);
    for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) if (Math.hypot(dx, dy) <= size) dab(x + dx, y + dy, tone);
  }
  const colour = new Uint8Array(N * N * 3), height = new Float32Array(N * N);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = x / N, v = y / N, i = y * N + x;
    const stain = stains(u, v), cloud = clouds(u, v), fine = grain(u, v), mark = Math.max(-1.5, Math.min(1.5, marks[i]));
    const light = 0.9 + (stain - 0.5) * 0.2 + (cloud - 0.5) * 0.1 + (fine - 0.5) * 0.07 + mark * 0.04;
    // Darker patches lean towards brown, as aged skin does.
    const age = Math.max(0, 0.55 - stain) * 1.6 + Math.max(0, -mark) * 0.12;
    colour[i * 3] = byte(light); colour[i * 3 + 1] = byte(light * (1 - 0.05 * age)); colour[i * 3 + 2] = byte(light * (1 - 0.16 * age));
    height[i] = fine * 0.5 + cloud * 0.6 + mark * 0.35;
  }
  png('T_Parchment.png', colour);
  png('T_Parchment_N.png', normals(height, 1.6));
}

// ---------- Wooden table ----------
// Dark walnut boards running across the texture, with long grain, the odd knot and seams between boards.
{
  const r = rng(1353);
  const BOARDS = 6, boardH = N / BOARDS;
  const boards = Array.from({ length: BOARDS }, () => ({ shift: r(), tone: 0.82 + r() * 0.3, warm: r(), knotU: r(), knotV: 0.25 + r() * 0.5, knot: r() < 0.5 }));
  const warp = layered(2, 12, 4, 51), streak = layered(4, 300, 3, 67), pores = layered(40, 512, 2, 83), wide = layered(2, 2, 3, 97);
  const colour = new Uint8Array(N * N * 3), height = new Float32Array(N * N);
  const dark = [0.028, 0.014, 0.007], mid = [0.085, 0.043, 0.02], lightC = [0.19, 0.105, 0.05];
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const b = Math.floor(y / boardH), board = boards[b];
    const u = (x / N + board.shift) % 1, v = (y % boardH) / boardH, i = y * N + x;
    // Growth rings seen side-on: wavy lines along the board, bending round a knot.
    let across = v + (warp(u, (b + v) / BOARDS) - 0.5) * 0.5;
    let knot = 0;
    if (board.knot) {
      let du = u - board.knotU; du -= Math.round(du);
      const d = Math.hypot(du * 5, (v - board.knotV) * 1.4);
      knot = Math.exp(-d * d * 40);
      across += Math.exp(-d * d * 6) * 0.22 * Math.sign(v - board.knotV || 1);
    }
    const rings = 0.5 + 0.5 * Math.sin(across * 46 + streak(u, (b + v) / BOARDS) * 5);
    const fibre = streak(u, (b + v) / BOARDS), pore = pores(u, (b + v) / BOARDS);
    let t = 0.12 + Math.pow(rings, 1.6) * 0.5 + (fibre - 0.5) * 0.85 + (wide(u, y / N) - 0.5) * 0.3;
    t = Math.max(0, Math.min(1, t * board.tone)) * (1 - knot * 0.75);
    const edge = Math.min(v, 1 - v) * boardH; // pixels from the seam
    const seam = edge < 1.5 ? 0.25 : edge < 4 ? 0.25 + 0.75 * ((edge - 1.5) / 2.5) : 1;
    const shade = (0.92 + (pore - 0.5) * 0.28) * seam;
    for (let c = 0; c < 3; c++) {
      const base = t < 0.5 ? dark[c] + (mid[c] - dark[c]) * (t / 0.5) : mid[c] + (lightC[c] - mid[c]) * ((t - 0.5) / 0.5);
      colour[i * 3 + c] = byte(Math.pow(base * shade * (c === 0 ? 1 + board.warm * 0.06 : 1), 1 / 2.2));
    }
    height[i] = rings * 0.12 + pore * 0.25 + (seam - 1) * 1.6 - knot * 0.3;
  }
  png('T_Wood.png', colour);
  png('T_Wood_N.png', normals(height, 2.2));
}

// ---------- Water ----------
// Small rounded ripples for the sea's surface: only a normal map, moved across the water by the material.
{
  const swell = layered(5, 5, 3, 211), ripple = layered(14, 14, 4, 233), fine = layered(48, 48, 2, 251);
  const height = new Float32Array(N * N);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = x / N, v = y / N;
    // Ridged noise gives the crests their sharper backs.
    const ridge = 1 - Math.abs(ripple(u, v) * 2 - 1);
    height[y * N + x] = swell(u, v) * 1.2 + ridge * 0.9 + fine(u, v) * 0.25;
  }
  png('T_Water_N.png', normals(height, 5));
}

// A plain mid-grey picture: the stand-in for the coast picture the game makes when it starts.
png('T_Flat.png', new Uint8Array(N * N * 3).fill(128));
