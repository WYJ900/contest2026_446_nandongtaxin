// The team repository owns these files; the workspace gateway is a runnable mirror.
// Default: compare only. --apply copies this fixed allowlist without deleting files.
import { readFile, writeFile, mkdir } from 'node:fs/promises';

const source = new URL('../web-gateway/', import.meta.url);
const target = new URL('../../web-gateway/', import.meta.url);
const files = [
  'package.json', 'README.md', 'server.mjs', 'start-mimo.ps1',
  'public/app.js', 'public/index.html', 'public/ride-data.js', 'public/styles.css',
  'test/app.test.mjs', 'test/prefill.test.mjs', 'test/server.test.mjs', 'test/start-mimo.test.mjs',
  'ai-provider.mjs', 'start-ai.ps1', 'public/ble-transport.js',
  'test/ai-provider.test.mjs', 'test/ble-transport.test.mjs',
  'watch-font.mjs',
];
const args = process.argv.slice(2);
if (args.some((arg) => arg !== '--check' && arg !== '--apply') ||
    (args.includes('--check') && args.includes('--apply'))) {
  throw new Error('Usage: node scripts/sync-web-gateway.mjs [--check | --apply]');
}
const apply = args.includes('--apply');
let differences = 0;
for (const file of files) {
  const expected = await readFile(new URL(file, source));
  const destination = new URL(file, target);
  let actual;
  try { actual = await readFile(destination); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  if (actual?.equals(expected)) continue;
  differences++;
  if (apply) {
    await mkdir(new URL('./', destination), { recursive: true });
    await writeFile(destination, expected);
  }
  console.log(`${apply ? 'SYNCED' : 'DIFF'} ${file}`);
}
console.log(`${files.length} files checked; ${differences} ${apply ? 'synchronized' : 'differences'}.`);
if (!apply && differences) process.exitCode = 1;
