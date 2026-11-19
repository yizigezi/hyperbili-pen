/**
 * BilibiliClient 组装与单例导出。
 * 沿 HyperBilibili (GPL-3.0) client.ts 的 mixin 组织方式。
 */
import { BiliClient } from './request.js';
import { BilibiliClientLoginMethods } from './login.js';
import { BilibiliClientMessageMethods } from './message.js';
import { BilibiliClientUserMethods } from './user.js';

// 将各模块的方法添加到 BilibiliClient 的 prototype 上
Object.assign(
  BiliClient.prototype,
  BilibiliClientLoginMethods,
  BilibiliClientMessageMethods,
  BilibiliClientUserMethods
);

// 全局单例（对应原版 global.biliclient）
export const biliclient = new BiliClient();
export default biliclient;
