/**
 * Encrypted credential vault adapter.
 * PBKDF2/AES work runs only in the native worker; this layer polls without blocking JS.
 */
let nativeApiPromise;
async function nativeApi() {
  if (!nativeApiPromise) nativeApiPromise = import('hyperCredential').then(m => m.default || m);
  const api = await nativeApiPromise;
  if (!api || typeof api.submit !== 'function' || typeof api.poll !== 'function') {
    throw new Error('原生凭证管理器不可用，拒绝使用明文存储');
  }
  return api;
}
async function run(op, password, data) {
  const api = await nativeApi();
  const id = api.submit(op, password || '', data == null ? null : String(data));
  return new Promise((resolve, reject) => {
    const started = Date.now();
    const poll = () => {
      let result;
      try { result = api.poll(id); } catch (error) { reject(error); return; }
      if (result === null) {
        if (Date.now() - started > 60000) { reject(new Error('凭证操作等待超时')); return; }
        setTimeout(poll, 25);
        return;
      }
      if (!result.ok) { reject(new Error(result.error || '凭证操作失败')); return; }
      resolve(result.data == null ? null : result.data);
    };
    poll();
  });
}
export async function store(password, data) { await run('store', password, data); }
export async function load(password) {
  try {
    const value = await run('load', password);
    return value ? JSON.parse(value) : null;
  } catch (error) {
    // Missing vault is a first-run state. Authentication failures remain explicit.
    if (String(error && error.message).indexOf('(-4)') >= 0) return null;
    throw error;
  }
}
export async function remove() { await run('delete', ''); }
export async function exists() { return (await run('exists', '')) === '1'; }
export default { store, load, remove, exists };
