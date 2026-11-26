/**
 * 网络适配层：统一不同运行环境的 HTTP 能力。
 *
 * 三级降级（纯前端，不依赖原生模块）：
 *   1) `$falcon.jsapi.http.request(opts, cb)` —— 笔上 Falcon 的 http jsapi
 *      （weatheronyddictpen 实证存在；响应形状未完全公开，故 rawResponse 原样保留）
 *   2) `import('http')` —— 若固件把它注册成 jsapi 模块
 *   3) `global.execShell('curl ...')` —— PenMusic 文档化的 shell 桥，curl -i 保头
 *
 * 对外：
 *   rawRequest({url, method, headers, body}) -> {status, headers, bodyText, rawResponse, source}
 *   getJson(url, headers)                     -> {status, json, raw}
 *   postForm(url, formBody, headers)          -> 同上（application/x-www-form-urlencoded）
 */
import logger from './logger.js';

// ---- execShell 桥（与 readex-nt FileSystem.js 同思路）----

async function getExecShell() {
  if (typeof global !== 'undefined' && global && typeof global.execShell === 'function') {
    return global.execShell.bind(global);
  }
  if (typeof globalThis !== 'undefined' && typeof globalThis.execShell === 'function') {
    return globalThis.execShell.bind(globalThis);
  }
  try {
    const module = await import('global');
    const Global = module.default || module.Global;
    const api = Global && typeof Global === 'function' ? new Global() : (Global || module);
    if (api && typeof api.execShell === 'function') {
      return api.execShell.bind(api);
    }
  } catch (e) {
    // fallthrough
  }
  throw new Error('当前环境无 execShell');
}

function shellQuote(value) {
  return "'" + String(value).replace(/'/g, "'\\''") + "'";
}

// ---- 响应归一化 ----

// 从 jsapi 回调的未知形状里扒出响应头（找不到返回 null，留给真机探针确认）
function extractHeaders(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const h = raw.headers || raw.header || raw.Headers || null;
  if (!h || typeof h !== 'object') return null;
  // 归一为小写键，但保留原始值（Set-Cookie 可能是数组）
  const out = {};
  for (const k of Object.keys(h)) {
    out[k.toLowerCase()] = h[k];
  }
  return out;
}

// 从 jsapi 回调的未知形状里扒出响应体文本
function extractBodyText(raw) {
  if (typeof raw === 'string') return raw;
  if (!raw || typeof raw !== 'object') return '';
  for (const key of ['result', 'body', 'text', 'data', 'response']) {
    const v = raw[key];
    if (typeof v === 'string') return v;
    if (v && typeof v === 'object') return JSON.stringify(v);
  }
  return '';
}

function extractStatus(raw, headers) {
  if (raw && typeof raw === 'object') {
    for (const key of ['status', 'statusCode', 'code']) {
      const v = raw[key];
      if (typeof v === 'number' && v >= 100 && v <= 599) return v;
    }
  }
  if (headers && typeof headers[':status'] === 'number') return headers[':status'];
  return 200; // 拿不到就当成功，靠业务 code 兜底
}

// ---- 三级请求实现 ----

async function requestViaFalconHttp({ url, method, headers, body }) {
  const http = (typeof $falcon !== 'undefined' && $falcon.jsapi && $falcon.jsapi.http)
    ? $falcon.jsapi.http
    : null;
  if (!http || typeof http.request !== 'function') {
    throw new Error('no $falcon.jsapi.http');
  }
  return new Promise((resolve, reject) => {
    const opts = {
      url,
      method: method || 'GET',
      header: headers || {}
    };
    if (body != null) opts.data = body;
    try {
      http.request(opts, (response) => {
        const head = extractHeaders(response);
        resolve({
          status: extractStatus(response, head),
          headers: head,
          bodyText: extractBodyText(response),
          rawResponse: response,
          source: 'falcon.jsapi.http'
        });
      });
    } catch (err) {
      reject(err);
    }
  });
}

async function requestViaHttpModule({ url, method, headers, body }) {
  const module = await import('http');
  const http = module.default || module;
  const fn = http.request || http.fetch || http.send;
  if (typeof fn !== 'function') throw new Error('http module has no request()');
  const response = await fn.call(http, {
    url,
    method: method || 'GET',
    header: headers || {},
    data: body != null ? body : undefined
  });
  const head = extractHeaders(response);
  return {
    status: extractStatus(response, head),
    headers: head,
    bodyText: extractBodyText(response),
    rawResponse: response,
    source: 'import(http)'
  };
}

async function requestViaCurl({ url, method, headers, body }) {
  const exec = await getExecShell();
  let cmd = 'curl -sS -i -X ' + shellQuote(method || 'GET') + ' ' + shellQuote(url);
  for (const k of Object.keys(headers || {})) {
    cmd += ' -H ' + shellQuote(k + ': ' + headers[k]);
  }
  if (body != null) {
    cmd += ' --data-binary ' + shellQuote(body);
  }
  const result = await exec(cmd);
  const text = typeof result === 'string' ? result
    : (result && (result.output || result.stdout || result.data || result.text)) || String(result);

  // 抓最后一个 HTTP 头块（curl -i 遇 3xx 时头可能有多段，body 在最后一段后）
  const parts = text.split(/\r?\n\r?\n/);
  // 从头往后找第一段以 HTTP/ 开头的作头块
  let headBlock = '';
  let bodyStart = 0;
  for (let i = 0; i < parts.length; i++) {
    if (/^HTTP\//.test(parts[i])) {
      headBlock = parts[i];
      bodyStart = i + 1;
      break;
    }
  }
  const head = {};
  const lines = headBlock.split(/\r?\n/);
  const statusLine = lines.shift() || '';
  for (const line of lines) {
    const idx = line.indexOf(':');
    if (idx > 0) {
      const k = line.slice(0, idx).trim().toLowerCase();
      const v = line.slice(idx + 1).trim();
      if (head[k]) head[k] = [head[k], v].flat().join(', ');
      else head[k] = v;
    }
  }
  const m = statusLine.match(/HTTP\/[\d.]+\s+(\d{3})/);
  return {
    status: m ? parseInt(m[1], 10) : 0,
    headers: head,
    bodyText: parts.slice(bodyStart).join('\n\n'),
    rawResponse: text,
    source: 'execShell(curl)'
  };
}

// ---- 对外接口 ----

export async function rawRequest({ url, method, headers, body }) {
  const tiers = [requestViaFalconHttp, requestViaHttpModule, requestViaCurl];
  let lastErr = null;
  for (const tier of tiers) {
    try {
      const res = await tier({ url, method, headers, body });
      logger.log('http[' + res.source + ']', method || 'GET', url, '->', res.status,
        res.headers === null ? '(headers 不可见)' : '');
      return res;
    } catch (err) {
      lastErr = err;
    }
  }
  throw lastErr || new Error('所有网络通道均不可用');
}

export async function getJson(url, headers) {
  const res = await rawRequest({ url, method: 'GET', headers });
  try {
    res.json = res.bodyText ? JSON.parse(res.bodyText) : null;
  } catch (e) {
    res.json = null;
  }
  return res;
}

export async function postForm(url, formBody, headers) {
  const h = Object.assign({ 'Content-Type': 'application/x-www-form-urlencoded' }, headers || {});
  return rawRequest({ url, method: 'POST', headers: h, body: formBody });
}

export default { rawRequest, getJson, postForm };
