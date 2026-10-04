// Renders the web map's own decorations (the "Europa" title cartouche and the
// compass rose from src/ui/art.js, with the web version's fonts) to PNG files
// in SourceArt/MapArt, using Google Chrome. Only reads the reference folder.
//   node Tools/make_map_art.mjs
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ref = new URL('../ports_web_reference_READ_ONLY/', import.meta.url);
const out = new URL('../SourceArt/MapArt/', import.meta.url);
mkdirSync(out, { recursive: true });
const { compassRose, cartouche } = await import(new URL('src/ui/art.js', ref));
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const font = (file) => new URL(`assets/fonts/${file}`, ref).href;

// name, the drawing, its box in the drawing's own units (x, y, width, height), and the picture's size.
const ART = [
  ['T_MapCartouche', cartouche('Europa', 'MCCCXLVII – MCCCLIII'), [-144, -36, 288, 72], [2048, 512]],
  ['T_MapCompass', compassRose(), [-64, -70, 128, 128], [1024, 1024]],
];

// One tile of the sea's wave pattern, as drawn by createMap in src/ui/map.js (46 x 26, two pale strokes).
const WAVES = '<path d="M2 13 q5 -5 10 0 t10 0" fill="none" stroke="#9fd3d6" stroke-width="1.2" opacity="0.45"/><path d="M25 3 q4 -4 8 0 t8 0" fill="none" stroke="#9fd3d6" stroke-width="1" opacity="0.3"/>';
ART.push(['T_SeaWaves', WAVES, [0, 0, 46, 26], [1024, 512]]);

for (const [name, svg, box, [w, h]] of ART) {
  const html = `<!doctype html><meta charset="utf-8"><style>
    @font-face { font-family: 'Cinzel'; src: url('${font('cinzel-latin-800-normal.woff2')}') format('woff2'); font-weight: 800; }
    @font-face { font-family: 'Unifraktur'; src: url('${font('unifrakturmaguntia-latin-400-normal.woff2')}') format('woff2'); }
    html, body { margin: 0; background: transparent; } svg { display: block; }
  </style><svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="${box.join(' ')}" preserveAspectRatio="none">${svg}</svg>`;
  const page = new URL(`${name}.html`, out);
  writeFileSync(page, html);
  execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1', '--default-background-color=00000000',
    '--virtual-time-budget=4000', `--window-size=${w},${h}`, `--screenshot=${fileURLToPath(new URL(`${name}.png`, out))}`, page.href], { stdio: 'ignore' });
  rmSync(page);
  console.log('made', `${name}.png`);
}
