// Renders the website's own artwork to PNG files for the Unreal screens: house
// crests and banners, action icons, card illustrations, dice faces, the small
// stat icons, and the walnut-table and candle-light backgrounds. Everything is
// drawn by the website's code and stylesheet (src/ui/art.js, src/styles/game.css)
// in Google Chrome, at twice the size for sharpness. Only reads the reference folder.
//   node Tools/make_ui_art.mjs
import { writeFileSync, mkdirSync, rmSync, existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ref = new URL('../ports_web_reference_READ_ONLY/', import.meta.url);
const out = new URL('../Content/UI/Art/', import.meta.url);
mkdirSync(out, { recursive: true });
const art = await import(new URL('src/ui/art.js', ref));
const { dieFaces } = await import(new URL('src/ui/dice.js', ref)).catch(() => ({ dieFaces: null }));
const { PLAYER_STYLES } = await import(new URL('src/engine/state.js', ref));
const actions = JSON.parse((await import('node:fs')).readFileSync(new URL('data/actions.json', ref), 'utf8')).actions;
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const css = new URL('src/styles/game.css', ref).href;
const ICONS = { ship: '⚓', post: '🏛', move: '🐎', prepare: '🚪', physician: '⚕', charity: '✝', marry: '💍', land: '🌾', loan: '📜', deal: '🤝', gates: '⛨' };
const PIPS = { 1: [4], 2: [0, 8], 3: [0, 4, 8], 4: [0, 2, 6, 8], 5: [0, 2, 4, 6, 8], 6: [0, 2, 3, 5, 6, 8] };
const faces = (v) => Array.from({ length: 9 }, (_, i) => (PIPS[v].includes(i) ? '<span class="pipdot"></span>' : '<span></span>')).join('');

// name, HTML for the body, width and height in CSS pixels.
const ART = [];
for (const s of PLAYER_STYLES) {
  ART.push([`crest_${s.crest}`, `<svg width="44" height="44" viewBox="0 0 22 22"><g fill="${s.color}" stroke="#1a1208" stroke-width="1.5">${art.crestPath(s.crest, 22)}</g></svg>`, 44, 44]);
  ART.push([`banner_${s.crest}`, `<div style="width:150px">${art.heraldicBanner(s.color, s.crest).replace('<svg ', '<svg style="display:block;width:100%;height:auto;overflow:visible" ')}</div>`, 150, 214]);
}
for (const a of actions) {
  ART.push([`action_${a.id}`, `<button class="action-btn" data-action="${a.id}" style="all:unset;display:block"><span class="icon" style="display:grid">${ICONS[a.id]}</span></button>`, 46, 46]);
}
for (const theme of Object.keys(art.THEME_COLORS)) {
  const t = art.THEME_COLORS[theme];
  ART.push([`illus_${theme}`, `<style>.card-band::after{content:none !important}</style><div class="card-band" style="--card:${t.main};--card-light:${t.light};background:none;padding:4px"><div class="illus">${art.themeIllustration(theme, 52)}</div></div>`, 78, 78]);
}
for (const kind of ['', 'red', 'gold']) {
  for (let v = 1; v <= 6; v++) {
    ART.push([`die_${kind || 'ivory'}_${v}`, `<div class="die ${kind}" style="perspective:none;margin:2px;width:60px;height:60px"><div class="die-face" style="transform:none;backface-visibility:visible">${faces(v)}</div></div>`, 64, 64]);
  }
}
ART.push(['icon_coin', art.coinIcon(40), 40, 40], ['icon_laurel', art.laurelIcon(40), 40, 40], ['icon_family', art.familyIcon(40), 40, 40]);
ART.push(['icon_candle_lit', art.candleIcon(true, 40), 28, 40], ['icon_candle_out', art.candleIcon(false, 40), 28, 40]);
// The flag that marks a trading post on the map (banner in art.js), one per house; the pole's foot is at the bottom, 6 from the left.
for (const s of PLAYER_STYLES) {
  ART.push([`post_${s.crest}`, `<svg style="display:block" width="48" height="60" viewBox="-6 -27 24 30">${art.banner(s.color, s.crest, 0)}</svg>`, 48, 60]);
}
// The finale's honours: a gold medal for each, with the same picture as on the website (AWARDS in finale.js, in that order).
const MEDALS = ['⛵', '👪', '💰', '🕯️', '🎡', '🐀', '🏛️', '🏃', '⭐', '💍', '🌾'];
MEDALS.forEach((icon, i) => {
  ART.push([`honour_${i}`, `<div style="padding:3px 3px 7px"><div class="honour-medal" style="width:78px;margin:0;font-size:2.3rem">${icon}</div></div>`, 84, 88]);
});
// The finale's candles (candleIcon in art.js): lit for a house that took a stand, unlit for one that did not.
ART.push(['vigil_lit', art.candleIcon(true, 180), 126, 180], ['vigil_out', art.candleIcon(false, 180), 126, 180]);
// The marks inside the top bar's round circles (.timeline span in game.css): an anchor before the plague, the season
// of each half-year, or a star for Quick Play's longer rounds; gold on a dark circle, dark on the bright present one.
for (const [name, icon] of [['anchor', '⚓'], ['sun', '☀'], ['snow', '❄'], ['star', '✦']]) {
  for (const [state, color] of [['off', '#f3d27a'], ['on', '#5c0d09']]) {
    ART.push([`track_${name}_${state}`, `<div style="width:18px;height:18px;display:grid;place-items:center;font-size:10px;line-height:1;color:${color};font-family:'EB Garamond',serif">${icon}&#xFE0E;</div>`, 18, 18]);
  }
}
// The map legend's little pictures (the rows of .legend in src/ui/map.js).
const hatch = '<defs><pattern id="hatch" width="5" height="5" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="5" height="5" fill="#b8322a"/><line x1="0" y1="0" x2="0" y2="5" stroke="#5c0f09" stroke-width="2"/></pattern></defs>';
const city = (inner) => `<svg style="display:block" width="26" height="22" viewBox="-13 -12 26 22">${hatch}${inner}</svg>`;
ART.push(['legend_safe', city(art.castle()), 26, 22]);
ART.push(['legend_threatened', city(`<circle r="10" fill="none" stroke="#e7a02b" stroke-width="3" stroke-dasharray="4 3"/>${art.castle()}`), 26, 22]);
ART.push(['legend_stricken', city(`<circle r="10" fill="none" stroke="#c0281c" stroke-width="3"/>${art.castle('url(#hatch)')}`), 26, 22]);
ART.push(['legend_aftermath', city(`<circle r="10" fill="none" stroke="#6f6a5f" stroke-width="3"/>${art.castle('#cfc8b8', '#55504a')}`), 26, 22]);
ART.push(['legend_post', `<svg style="display:block" width="26" height="26" viewBox="-6 -20 22 28">${art.banner('#888', 'circle', 2)}</svg>`, 26, 26]);
ART.push(['legend_sea', '<svg style="display:block" width="26" height="10"><line x1="1" y1="5" x2="25" y2="5" stroke="#10375c" stroke-width="2.5" stroke-dasharray="1 4" stroke-linecap="round"/></svg>', 26, 10]);
ART.push(['legend_land', '<svg style="display:block" width="26" height="10"><line x1="1" y1="5" x2="25" y2="5" stroke="#6b4423" stroke-width="2.5" stroke-dasharray="6 4"/></svg>', 26, 10]);
// Backgrounds: the walnut table (the page behind the setup screen), the darkening over the title map, and the veil behind cards.
ART.push(['bg_wood', '<div style="width:960px;height:540px"></div>', 960, 540, 'body']);
ART.push(['bg_title_veil', '<div style="width:640px;height:360px;background:radial-gradient(ellipse at center, rgba(20, 10, 2, 0.05), rgba(20, 10, 2, 0.78))"></div>', 640, 360]);
ART.push(['bg_dialog_veil', '<div style="width:640px;height:360px;background:radial-gradient(ellipse at center, rgba(30, 18, 6, 0.6), rgba(10, 5, 0, 0.82))"></div>', 640, 360]);

const only = process.argv[2];
for (const [name, body, w, h, keepBody] of ART) {
  if (only && !name.startsWith(only)) continue;
  const page = new URL(`${name}.html`, out);
  writeFileSync(page, `<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="${css}"><style>
    html, body { margin: 0; ${keepBody ? '' : 'background: transparent !important;'} min-height: 0; overflow: hidden; }
    * { animation: none !important; }
  </style>${body}`);
  execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=2', '--default-background-color=00000000',
    '--virtual-time-budget=3000', `--window-size=${w},${h}`, `--screenshot=${fileURLToPath(new URL(`${name}.png`, out))}`, page.href], { stdio: 'ignore', timeout: 60000 });
  rmSync(page);
  if (!existsSync(new URL(`${name}.png`, out))) throw new Error(`Chrome did not write ${name}.png`);
}
console.log(`made ${ART.length} pictures in Content/UI/Art`);
