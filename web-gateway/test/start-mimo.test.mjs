import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const launcherPath = fileURLToPath(new URL('../start-mimo.ps1', import.meta.url));

test('MiMo 启动脚本安全读取 Token 且不把凭据写入文件', () => {
  assert.equal(existsSync(launcherPath), true, '缺少 start-mimo.ps1');

  const script = readFileSync(launcherPath, 'utf8');
  assert.match(script, /Read-Host.+-AsSecureString/);
  assert.match(script, /ANTHROPIC_AUTH_TOKEN/);
  assert.match(script, /ANTHROPIC_BASE_URL/);
  assert.match(script, /ANTHROPIC_MODEL/);
  assert.match(script, /mimo-v2\.5-pro/);
  assert.match(script, /ZeroFreeBSTR/);
  assert.doesNotMatch(script, /tp-[A-Za-z0-9_-]{8,}/);
});
