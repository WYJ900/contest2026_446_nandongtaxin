import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

// Large UI font for titles, list rows and buttons. Only characters that
// appear in string literals of the UI sources are included; anything else
// (for example AI review text) falls back to velaride_font_cn_20.

const arg = (name) => process.argv[process.argv.indexOf(name) + 1];
const check = process.argv.includes('--check');
if (!check && (!process.argv.includes('--converter') || !process.argv.includes('--font'))) {
  throw new Error('Usage: node scripts/build-watch-ui-font.mjs --converter <lv_font_conv.js> --font <NotoSansSC-SemiBold-600.ttf>');
}

const sources = ['velaride_shell.c', 'velaride_sport_ui.c', 'velaride_theme.h'];
const chars = new Set();
for (const name of sources) {
  const code = readFileSync(new URL(`../app/velaride/${name}`, import.meta.url), 'utf8');
  for (const [, literal] of code.matchAll(/"((?:[^"\\\n]|\\.)*)"/g)) {
    for (const ch of literal) {
      if (ch.codePointAt(0) > 0x7f) chars.add(ch);
    }
  }
}
const symbols = [...chars].sort().join('');
const output = process.argv.includes('--output') ? pathToFileURL(resolve(arg('--output'))) :
  new URL('../app/velaride/velaride_font_cn_28.c', import.meta.url);

if (check) {
  // Fail when the UI uses a character the generated font does not contain.
  const header = readFileSync(output, 'utf8').match(/--symbols (\S+)/);
  const have = new Set(header ? [...header[1]] : []);
  const missing = [...chars].filter((ch) => !have.has(ch)).join('');
  if (missing) {
    console.error(`velaride_font_cn_28 is missing: ${missing}; rerun this script without --check`);
    process.exit(1);
  }
  console.log(`velaride_font_cn_28 covers all ${chars.size} UI characters.`);
  process.exit(0);
}

execFileSync(process.execPath, [arg('--converter'), '--font', arg('--font'),
  '--symbols', symbols, '-r', '0x20-0x7E', '--size', '28', '--bpp', '4',
  '--format', 'lvgl', '--no-compress', '--lv-include', 'lvgl/lvgl.h',
  '--lv-font-name', 'velaride_font_cn_28', '--lv-fallback', 'velaride_font_cn_20',
  '-o', fileURLToPath(output),
], { stdio: 'inherit' });
console.log(`Generated ${[...symbols].length} CJK glyphs plus ASCII at 28 px.`);
