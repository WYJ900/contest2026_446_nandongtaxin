// Credentials stay in the server process. Only publicConfig() reaches the UI.
export function resolveConfig(env = process.env) {
  const custom = ['AI_PROTOCOL', 'AI_BASE_URL', 'AI_MODEL', 'AI_API_KEY']
    .some((key) => Boolean(env[key]));
  let config;
  if (custom) {
    config = {
      protocol: env.AI_PROTOCOL || 'openai', baseUrl: env.AI_BASE_URL || '',
      model: env.AI_MODEL || '', key: env.AI_API_KEY || '',
      name: env.AI_PROVIDER_NAME || '自定义 AI', auth: env.AI_AUTH || 'bearer',
      source: `compatible-${env.AI_PROTOCOL || 'openai'}`,
    };
  } else if (env.ANTHROPIC_AUTH_TOKEN) {
    config = {
      protocol: 'anthropic', baseUrl: env.ANTHROPIC_BASE_URL || '',
      model: env.ANTHROPIC_MODEL || 'mimo-v2.5', key: env.ANTHROPIC_AUTH_TOKEN,
      name: 'Xiaomi MiMo', auth: 'bearer', source: 'mimo-anthropic',
      thinking: { type: 'disabled' },
    };
  } else {
    config = {
      protocol: 'openai', baseUrl: env.MIMO_BASE_URL || 'https://api.xiaomimimo.com/v1',
      model: env.MIMO_MODEL || 'mimo-v2-flash', key: env.MIMO_API_KEY || '',
      name: 'Xiaomi MiMo', auth: 'bearer', source: 'mimo-openai',
    };
  }
  config.timeoutMs = Number(env.AI_TIMEOUT_MS || 60000);
  if (!['openai', 'responses', 'anthropic'].includes(config.protocol)) {
    throw new Error('AI_PROTOCOL 必须是 openai、responses 或 anthropic');
  }
  if (!['bearer', 'x-api-key'].includes(config.auth)) throw new Error('AI_AUTH 不受支持');
  if (!Number.isInteger(config.timeoutMs) || config.timeoutMs < 1000 || config.timeoutMs > 60000) {
    throw new Error('AI_TIMEOUT_MS 必须介于 1000 和 60000');
  }
  if (custom && (!config.baseUrl || !config.model)) throw new Error('请填写 AI_BASE_URL 和 AI_MODEL');
  config.endpoint = endpointFor(config.baseUrl, config.protocol);
  // DeepSeek documents high-effort thinking as its default. Short watch
  // summaries use non-thinking mode; set AI_THINKING=provider to opt out.
  config.fastReview = /^deepseek[-/]/i.test(config.model) && env.AI_THINKING !== 'provider';
  if (config.protocol === 'anthropic' &&
      new URL(config.endpoint).hostname.endsWith('.xiaomimimo.com')) {
    config.thinking = { type: 'disabled' };
  }
  return config;
}

export function endpointFor(baseUrl, protocol) {
  let url;
  try { url = new URL(baseUrl); }
  catch { throw new Error('AI_BASE_URL 必须是完整的 HTTP(S) 地址'); }
  if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password || url.search || url.hash) {
    throw new Error('AI_BASE_URL 不得包含账号、密码、查询参数或片段');
  }
  let path = url.pathname.replace(/\/+$/, '');
  const suffix = { openai: '/chat/completions', responses: '/responses', anthropic: '/messages' }[protocol];
  if (!suffix) throw new Error('未知接口协议');
  const existing = ['/chat/completions', '/responses', '/messages'].find((end) => path.endsWith(end));
  if (existing && existing !== suffix) throw new Error('接口地址与所选协议不匹配');
  if (!existing) {
    if (!path || (protocol === 'anthropic' && !path.endsWith('/v1'))) path += '/v1';
    path += suffix;
  }
  url.pathname = path;
  return url.toString();
}

export function publicConfig(config) {
  return { provider: config.name, model: config.model, protocol: config.protocol,
    configured: Boolean(config.key) };
}

function blockText(blocks) {
  if (typeof blocks === 'string') return blocks;
  if (!Array.isArray(blocks)) return '';
  return blocks.filter((part) => ['text', 'output_text'].includes(part?.type))
    .map((part) => typeof part.text === 'string' ? part.text : '').join('');
}

export function extractText(data, protocol) {
  let text;
  if (protocol === 'anthropic') text = blockText(data?.content);
  else if (protocol === 'responses') {
    text = typeof data?.output_text === 'string' ? data.output_text :
      (Array.isArray(data?.output) ? data.output : [])
        .filter((item) => item.type === 'message' && item.role === 'assistant')
        .map((item) => blockText(item.content)).join('');
  } else text = blockText(data?.choices?.[0]?.message?.content);
  if (!text?.trim()) {
    const error = new Error('AI 返回内容为空或格式不兼容');
    error.code = data?.stop_reason === 'max_tokens' ? 'output_limit' : 'empty_response';
    throw error;
  }
  return text.trim();
}

export async function requestReview(config, ride, system) {
  if (!config.key) return null;
  const headers = { 'content-type': 'application/json' };
  if (config.auth === 'x-api-key') headers['x-api-key'] = config.key;
  else headers.authorization = `Bearer ${config.key}`;
  let body;
  // Zero is the legacy placeholder when GPS/distance was not supplied.
  // Do not let a model turn that absence into a measured zero-kilometre ride.
  const { distance_km, ...metrics } = ride;
  const user = JSON.stringify(distance_km > 0 ? ride : {
    ...metrics, distance_status: 'not_recorded',
  });
  if (config.protocol === 'anthropic') {
    headers['anthropic-version'] = '2023-06-01';
    body = { model: config.model, max_tokens: 4096, system,
      messages: [{ role: 'user', content: user }] };
    if (config.thinking || config.fastReview) body.thinking = config.thinking || { type: 'disabled' };
  } else if (config.protocol === 'responses') {
    body = { model: config.model, instructions: system, input: user,
      max_output_tokens: 2048, store: false };
    if (config.fastReview) body.reasoning = { effort: 'none' };
  } else {
    // Avoid model-specific temperature/thinking flags on generic relays.
    body = { model: config.model, messages: [
      { role: 'system', content: system }, { role: 'user', content: user },
    ] };
    if (config.fastReview) body.thinking = { type: 'disabled' };
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), config.timeoutMs);
  try {
    const response = await fetch(config.endpoint, {
      method: 'POST', headers, body: JSON.stringify(body),
      signal: controller.signal, redirect: 'error',
    });
    if (!response.ok) {
      const error = new Error(`AI HTTP ${response.status}`);
      error.status = response.status;
      throw error;
    }
    return extractText(await response.json(), config.protocol);
  } finally { clearTimeout(timer); }
}

export function clipUtf8(text, maxBytes = 260) {
  let output = '';
  let bytes = 0;
  for (const char of text) {
    const length = Buffer.byteLength(char);
    if (bytes + length > maxBytes) break;
    output += char;
    bytes += length;
  }
  return output;
}
