import { readFileSync, writeFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const arg = (name) => process.argv[process.argv.indexOf(name) + 1];
if (!process.argv.includes('--converter') || !process.argv.includes('--font')) {
  throw new Error('Usage: node scripts/build-watch-font.mjs --converter <lv_font_conv.js> --font <NotoSansSC-SemiBold-600.ttf>');
}
const symbols = [...new Set(readFileSync(new URL('../app/velaride/watch-font-symbols.txt', import.meta.url), 'utf8')
  .replace(/\s/g, ''))].sort().join('');
execFileSync(process.execPath, [arg('--converter'), '--font', arg('--font'),
  '--symbols', symbols, '--size', '19', '--bpp', '2', '--format', 'lvgl',
  '--no-compress', '--lv-include', 'lvgl/lvgl.h', '--lv-font-name', 'velaride_font_cn_20',
  '--lv-fallback', 'lv_font_montserrat_20', '-o',
  fileURLToPath(new URL('../app/velaride/velaride_font_cn_20.c', import.meta.url)),
], { stdio: 'inherit' });
writeFileSync(new URL('../web-gateway/watch-font.mjs', import.meta.url),
  `// Generated from app/velaride/watch-font-symbols.txt with the firmware font.\nexport const WATCH_FONT_CHARACTERS = ${JSON.stringify(symbols)};\n`);
console.log(`Generated ${[...symbols].length} glyphs and matching gateway character set.`);
