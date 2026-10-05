// Draws the game's app icon and saves it in the sizes a Mac app needs
// (Build/Mac/Resources/Assets.xcassets). The picture is the web version's own
// browser-tab icon (the ship in index.html: a blue hull and a red sail on
// parchment), drawn large in the game's colours with a gold-ruled border.
//   node Tools/make_icon.mjs
import { writeFileSync, mkdirSync, rmSync, cpSync, existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const ENGINE = process.env.UE_ROOT ?? '/Users/Shared/Epic Games/UE_5.8';
const assets = fileURLToPath(new URL('../Build/Mac/Resources/Assets.xcassets/', import.meta.url));
const source = fileURLToPath(new URL('../SourceArt/Icon/', import.meta.url));
mkdirSync(source, { recursive: true });
// The catalogue's layout (which sizes, and their names) is Unreal's own.
if (!existsSync(assets)) cpSync(`${ENGINE}/Engine/Build/Mac/Resources/Assets.xcassets`, assets, { recursive: true });

// A Mac icon is a rounded square that fills about four fifths of its canvas.
const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <defs>
    <radialGradient id="page" cx="0.45" cy="0.36" r="0.8"><stop offset="0" stop-color="#fffaf0"/><stop offset="0.6" stop-color="#f4e8c8"/><stop offset="1" stop-color="#dcc07c"/></radialGradient>
    <linearGradient id="sail" x1="0" x2="1"><stop offset="0" stop-color="#c93a2c"/><stop offset="1" stop-color="#8a1a10"/></linearGradient>
    <linearGradient id="hull" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="#2a5d9e"/><stop offset="1" stop-color="#16396a"/></linearGradient>
    <linearGradient id="sea" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="#26788a"/><stop offset="1" stop-color="#1b5a6e"/></linearGradient>
    <clipPath id="inside"><rect x="136" y="136" width="752" height="752" rx="150"/></clipPath>
    <filter id="drop" x="-20%" y="-20%" width="140%" height="140%"><feDropShadow dx="0" dy="14" stdDeviation="18" flood-color="#000" flood-opacity="0.35"/></filter>
  </defs>
  <g filter="url(#drop)">
    <rect x="100" y="100" width="824" height="824" rx="186" fill="#3b2413"/>
    <rect x="112" y="112" width="800" height="800" rx="174" fill="#d9a82b"/>
    <rect x="128" y="128" width="768" height="768" rx="158" fill="#7a560c"/>
    <rect x="136" y="136" width="752" height="752" rx="150" fill="url(#page)"/>
  </g>
  <g clip-path="url(#inside)">
    <path d="M136 690 q47 -34 94 0 t94 0 t94 0 t94 0 t94 0 t94 0 t94 0 t94 0 V900 H136z" fill="url(#sea)"/>
    <path d="M136 742 q47 -30 94 0 t94 0 t94 0 t94 0 t94 0 t94 0 t94 0 t94 0" fill="none" stroke="#7fc4c4" stroke-width="10" opacity="0.55"/>
    <path d="M136 806 q47 -30 94 0 t94 0 t94 0 t94 0 t94 0 t94 0 t94 0 t94 0" fill="none" stroke="#7fc4c4" stroke-width="10" opacity="0.35"/>
  </g>
  <!-- The ship of the web version's icon, twelve times its size: hull M12 40h40l-6 10H18z, sail M32 38V10l14 22z. -->
  <g transform="translate(128 132) scale(12)" stroke-linejoin="round">
    <line x1="32" y1="9" x2="32" y2="40" stroke="#3b2413" stroke-width="1.6" stroke-linecap="round"/>
    <path d="M32 38V10l14 22z" fill="url(#sail)" stroke="#3b2413" stroke-width="1.1"/>
    <path d="M30 38V15L19 33z" fill="#fff8e2" stroke="#3b2413" stroke-width="1.1"/>
    <path d="M32 9.4 l7 2.2 l-7 2.2z" fill="#d9a82b" stroke="#3b2413" stroke-width="0.6"/>
    <path d="M12 40h40l-6 10H18z" fill="url(#hull)" stroke="#3b2413" stroke-width="1.1"/>
    <path d="M14.6 43.6h34.8" stroke="#f3d27a" stroke-width="1.1" stroke-linecap="round"/>
  </g>
</svg>`;
writeFileSync(`${source}icon.svg`, svg);
const page = `${source}icon.html`;
writeFileSync(page, `<!doctype html><meta charset="utf-8"><style>html,body{margin:0;background:transparent}svg{display:block}</style>${svg}`);
execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1', '--default-background-color=00000000', '--window-size=1024,1024', `--screenshot=${source}icon_1024.png`, `file://${page}`], { stdio: 'ignore', timeout: 60000 });
rmSync(page);

for (const set of ['AppIcon.appiconset', 'UProject.iconset']) {
  for (const [name, size] of [['16x16', 16], ['16x16@2x', 32], ['32x32', 32], ['32x32@2x', 64], ['128x128', 128], ['128x128@2x', 256], ['256x256', 256], ['256x256@2x', 512], ['512x512', 512], ['512x512@2x', 1024]]) {
    execFileSync('sips', ['-z', String(size), String(size), `${source}icon_1024.png`, '--out', `${assets}${set}/icon_${name}.png`], { stdio: 'ignore' });
  }
}
console.log('made the app icon in Build/Mac/Resources/Assets.xcassets');
