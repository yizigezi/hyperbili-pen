/**
 * build:web 预览用 storage mock（模块名 'storage'）。
 * 真机上由 utils/storage.js 走 jsapi / FakeStorage，此文件仅参与 web 构建。
 */
const _store = {};

export default {
  async getStorage(key) {
    return Object.prototype.hasOwnProperty.call(_store, key) ? _store[key] : null;
  },
  async setStorage(key, value) {
    _store[key] = value;
    return 0;
  },
  async deleteStorage(key) {
    delete _store[key];
    return 0;
  }
};
