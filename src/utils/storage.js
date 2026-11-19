/**
 * 存储适配层：统一不同运行环境的 storage 能力。
 *
 * 优先级：
 *   1) `import('storage')` jsapi（doge-comic / readex 实证）
 *      - promise 形状：getStorage(key)/setStorage(key,value)/deleteStorage(key)
 *      - 回调形状：get({key,success,fail})/set({key,value,...})/delete({key,...})
 *   2) 内存 FakeStorage（web 预览 / 固件缺模块时降级，不持久化）
 *
 * 对外（一律 Promise，值一律字符串）：
 *   get(key) -> string|null
 *   set(key, value)
 *   del(key)
 */

const memory = new Map();
let impl = null; // {kind, mod}

function wrapPromiseShape(mod) {
  return {
    kind: 'storage(promise)',
    async get(key) {
      const v = await mod.getStorage(key);
      return v == null ? null : String(v);
    },
    async set(key, value) {
      await mod.setStorage(key, String(value));
    },
    async del(key) {
      if (typeof mod.deleteStorage === 'function') await mod.deleteStorage(key);
      else if (typeof mod.removeStorage === 'function') await mod.removeStorage(key);
      else await mod.setStorage(key, '');
    }
  };
}

function wrapCallbackShape(mod) {
  function call(fn, params) {
    return new Promise((resolve, reject) => {
      fn.call(mod, Object.assign({}, params, {
        success: (data) => resolve(data),
        fail: (data, code) => reject(new Error('storage fail code=' + code))
      }));
    });
  }
  return {
    kind: 'storage(callback)',
    async get(key) {
      const v = await call(mod.get, { key });
      return v == null || v === '' ? null : String(v);
    },
    async set(key, value) {
      await call(mod.set, { key, value: String(value) });
    },
    async del(key) {
      await call(mod.delete, { key });
    }
  };
}

const fakeImpl = {
  kind: 'FakeStorage(memory)',
  async get(key) {
    return memory.has(key) ? memory.get(key) : null;
  },
  async set(key, value) {
    memory.set(key, String(value));
  },
  async del(key) {
    memory.delete(key);
  }
};

async function getImpl() {
  if (impl) return impl;
  try {
    const module = await import('storage');
    const mod = module.default || module;
    if (typeof mod.getStorage === 'function' && typeof mod.setStorage === 'function') {
      impl = wrapPromiseShape(mod);
    } else if (typeof mod.get === 'function' && typeof mod.set === 'function') {
      impl = wrapCallbackShape(mod);
    } else {
      throw new Error('storage 模块形状不明');
    }
  } catch (err) {
    impl = fakeImpl;
  }
  return impl;
}

export async function get(key) {
  return (await getImpl()).get(key);
}

export async function set(key, value) {
  return (await getImpl()).set(key, value);
}

export async function del(key) {
  return (await getImpl()).del(key);
}

export async function storageKind() {
  return (await getImpl()).kind;
}

export default { get, set, del, storageKind };
