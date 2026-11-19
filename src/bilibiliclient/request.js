/**
 * BilibiliClient 核心：Cookie 组装、Wbi 签名、GET/POST 请求。
 * 移植自 HyperBilibili (GPL-3.0) src/bilibiliclient/client.ts 与 api/request.ts。
 *
 * 与原版的差异（详见 PLAN.md「API 映射」）：
 *   - `this.fetch.fetch`（QuickApp 系统 fetch）→ utils/http.js 适配层
 *   - `crypto.hashDigest`（Vela crypto）→ utils/md5.js 纯 JS MD5
 *   - getRequest/postRequest 保持 QuickApp fetch 的返回信封 {code, data, headers}
 *     形状，`data` 为解析后的 JSON body，call site 无需改动
 */
import { md5 } from '../utils/md5.js';
import http from '../utils/http.js';
import logger from '../utils/logger.js';
import { getMixinKey } from './utils.js';

export class BiliClient {
  constructor() {
    // 版本号
    this.version = '0.1.0';

    // 扫码登录用的二维码Key（临时存储）
    this.qrCodeKey = null;

    // B站Cookies
    this.sessData = null;
    this.biliJct = null;
    this.dedeUserID = null;
    this.sid = null;

    // B站账号信息（nav 接口返回）
    this.accountInfo = null;

    // 风控Cookies（不存储，刷新时更新）
    this.buvid3 = null;
    this.buvid4 = null;

    // 私信DeviceId，每次启动刷新
    // From https://github.com/andywang425/BLTH/
    this.dm_deviceid = 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (name) {
      const randomInt = (16 * Math.random()) | 0;
      return (name === 'x' ? randomInt : ((3 & randomInt) | 8)).toString(16).toUpperCase();
    });
  }

  // 获取请求头，用于模拟正常浏览器环境，降低风控概率
  getHeaders() {
    return {
      'Accept':
        'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/jxl,image/webp,image/png,image/svg+xml,*/*;q=0.8',
      'Accept-Encoding': '',
      'Accept-Language': 'zh-CN,zh;q=0.8',
      'Cookie': this.getCookieString(),
      'Referer': 'https://www.bilibili.com',
      'Origin': 'https://www.bilibili.com',
      'User-Agent':
        'Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:129.0) Gecko/20100101 Firefox/129.0'
    };
  }

  // 构建Cookie字符串
  getCookieString() {
    let cookies = `buvid3=${this.buvid3}; buvid4=${this.buvid4}; `;
    if (this.sessData) {
      cookies += `SESSDATA=${this.sessData}; bili_jct=${this.biliJct}; DedeUserID=${this.dedeUserID}; sid=${this.sid}; `;
    }
    // 专栏保持旧版
    cookies += 'opus-goback=1';
    return cookies;
  }

  // Wbi签名
  encWbi(params, img_key, sub_key) {
    const mixin_key = getMixinKey(img_key + sub_key);
    const curr_time = Math.floor(Date.now() / 1000);

    params.wts = curr_time; // 添加时间戳字段
    const query = Object.keys(params)
      .sort()
      .map(
        (key) =>
          `${encodeURIComponent(key)}=${encodeURIComponent(
            params[key].toString().replace(/[!'()*]/g, '')
          )}`
      )
      .join('&');

    const wbi_sign = md5(query + mixin_key);
    logger.log('wbi_sign: ' + wbi_sign);

    return `${query}&w_rid=${wbi_sign}`;
  }

  // 发送GET请求，返回 {code, data, headers} 信封（data 为解析后的 JSON body）
  async getRequest(url, responseType = 'json') {
    logger.log('getRequest: ' + url);
    try {
      const res = await http.getJson(url, this.getHeaders());
      return { code: res.status, data: res.json, headers: res.headers, rawText: res.bodyText };
    } catch (error) {
      logger.error('GET请求失败，详细数据：', error);
    }
    return undefined;
  }

  // 发送带Wbi签名的GET请求
  async getRequestWbi(url, params) {
    const img_key = this.accountInfo.wbi_img.img_url.split('/').pop().split('.')[0];
    const sub_key = this.accountInfo.wbi_img.sub_url.split('/').pop().split('.')[0];
    const signedParams = this.encWbi(params, img_key, sub_key);
    return this.getRequest(`${url}?${signedParams}`);
  }

  // 发送POST请求
  async postRequest(url, data, content_type, custom_headers = null) {
    logger.log(`postRequest: ${url}, body: ${data}, contentType: ${content_type}`);
    let headers = { ...this.getHeaders(), 'Content-Type': content_type };
    if (custom_headers) {
      headers = custom_headers;
    }
    try {
      const res = await http.rawRequest({ url, method: 'POST', headers, body: data });
      let json = null;
      try {
        json = res.bodyText ? JSON.parse(res.bodyText) : null;
      } catch (e) {
        json = null;
      }
      return { code: res.status, data: json, headers: res.headers, rawText: res.bodyText };
    } catch (error) {
      logger.error('POST请求失败，详细数据：', error);
    }
    return undefined;
  }

  // 发送带Wbi签名的POST请求
  async postRequestWbi(url, wbiParams, data, content_type, custom_headers = null) {
    const img_key = this.accountInfo.wbi_img.img_url.split('/').pop().split('.')[0];
    const sub_key = this.accountInfo.wbi_img.sub_url.split('/').pop().split('.')[0];
    const signedParams = this.encWbi(wbiParams, img_key, sub_key);
    return this.postRequest(`${url}?${signedParams}`, data, content_type, custom_headers);
  }
}
