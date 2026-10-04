// Records the web version's sound effects as WAV files for the Unreal build.
// The web game has no sound files: every effect is made by its own code
// (src/ui/sound.js) with the browser's Web Audio. This script loads that same
// code in Google Chrome, plays each effect into an offline recorder (through
// the game's own "stone hall" echo), and saves what comes out in
// SourceArt/Sounds. It only reads the reference folder.
//   node Tools/make_sounds.mjs
import { createServer } from 'node:http';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { extname, join, normalize } from 'node:path';

const ref = fileURLToPath(new URL('../ports_web_reference_READ_ONLY/', import.meta.url));
const out = fileURLToPath(new URL('../SourceArt/Sounds/', import.meta.url));
mkdirSync(out, { recursive: true });
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

// name, seconds to record, and how the effect is called.
const EFFECTS = [
  ['click', 1.2, 'click()'], ['dice', 3.2, 'dice(1200)'], ['bell', 5.5, 'bell()'], ['knell', 7.5, 'knell()'], ['page', 2.2, 'page()'], ['coin', 2.4, 'coin()'],
  ['sail', 4.0, 'sail()'], ['cart', 3.4, 'cart()'], ['fortune', 3.8, 'fortune()'], ['misfortune', 3.2, 'misfortune()'], ['plague', 5.0, 'plague()'],
  ['fanfare', 3.2, 'fanfare()'], ['victory', 7.0, 'victory()'], ['drumroll', 6.0, 'drumroll(2.2)'], ['stamp', 2.6, 'stamp()'], ['error', 2.4, 'error()'], ['low', 3.6, 'low()'],
];

const page = `<!doctype html><meta charset="utf-8"><script type="module">
const RATE = 44100;
const effects = ${JSON.stringify(EFFECTS)};
for (const [name, seconds, call] of effects) {
  // The game's code asks for an ordinary audio context; it is handed a recorder instead.
  const recorder = new OfflineAudioContext(2, Math.ceil(RATE * seconds), RATE);
  window.AudioContext = function () { return recorder; };
  try { localStorage.removeItem('ports-of-plague-muted'); } catch {}
  const { sfx } = await import('/src/ui/sound.js?take=' + name);
  new Function('sfx', 'sfx.' + call)(sfx);
  const buffer = await recorder.startRendering();
  const left = buffer.getChannelData(0), right = buffer.getChannelData(1);
  // Cut the silence off the end, leaving a short tail.
  let end = left.length - 1;
  while (end > 0 && Math.abs(left[end]) < 0.0004 && Math.abs(right[end]) < 0.0004) end--;
  end = Math.min(left.length, end + Math.floor(RATE * 0.05));
  const pcm = new Int16Array(end * 2);
  for (let i = 0; i < end; i++) {
    pcm[i * 2] = Math.max(-32768, Math.min(32767, Math.round(left[i] * 32767)));
    pcm[i * 2 + 1] = Math.max(-32768, Math.min(32767, Math.round(right[i] * 32767)));
  }
  await fetch('/save?name=' + name, { method: 'POST', body: pcm.buffer });
}
await fetch('/done', { method: 'POST' });
</script>`;

const TYPES = { '.js': 'text/javascript', '.json': 'application/json', '.css': 'text/css', '.html': 'text/html' };
let chrome = null;
const made = [];
const server = createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (req.method === 'POST') {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => {
      res.end('ok');
      if (url.pathname === '/done') {
        console.log(`made ${made.length} sounds in SourceArt/Sounds: ${made.join(', ')}`);
        chrome?.kill();
        server.close();
        process.exit(made.length === EFFECTS.length ? 0 : 1);
      }
      const data = Buffer.concat(chunks);
      const name = url.searchParams.get('name');
      // A plain 16-bit stereo WAV file.
      const head = Buffer.alloc(44);
      head.write('RIFF', 0); head.writeUInt32LE(36 + data.length, 4); head.write('WAVEfmt ', 8); head.writeUInt32LE(16, 16); head.writeUInt16LE(1, 20); head.writeUInt16LE(2, 22);
      head.writeUInt32LE(44100, 24); head.writeUInt32LE(44100 * 4, 28); head.writeUInt16LE(4, 32); head.writeUInt16LE(16, 34); head.write('data', 36); head.writeUInt32LE(data.length, 40);
      writeFileSync(join(out, `sfx_${name}.wav`), Buffer.concat([head, data]));
      made.push(name);
    });
    return;
  }
  if (url.pathname === '/record.html') { res.setHeader('Content-Type', 'text/html'); res.end(page); return; }
  const file = normalize(join(ref, url.pathname));
  if (!file.startsWith(ref) || !existsSync(file)) { res.statusCode = 404; res.end('not found'); return; }
  res.setHeader('Content-Type', TYPES[extname(file)] ?? 'application/octet-stream');
  res.end(readFileSync(file));
});
server.listen(0, '127.0.0.1', () => {
  const port = server.address().port;
  chrome = spawn(CHROME, ['--headless=new', '--disable-gpu', '--autoplay-policy=no-user-gesture-required', `--user-data-dir=/tmp/ports-sounds-${port}`, `http://127.0.0.1:${port}/record.html`], { stdio: 'ignore' });
  setTimeout(() => { console.error('Chrome did not finish in time.'); chrome.kill(); process.exit(1); }, 120000);
});
