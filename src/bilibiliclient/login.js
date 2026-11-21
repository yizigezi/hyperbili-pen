/**
 * 登录相关方法（挂到 BiliClient.prototype 的 mixin）。
 * 移植自 HyperBilibili (GPL-3.0) src/bilibiliclient/account/login.ts。
 *
 * 差异：
 *   - storage jsapi 回调式 → utils/storage.js Promise 适配层
 *   - 原版 Set-Cookie 取 `response.headers['Set-Cookie']`，适配层统一小写键，
 *     此处取 `response.headers['set-cookie']`
 *   - 原版 session 失效跳错误页（router），此处返回对应结果由页面处理
 */
import storage from '../utils/storage.js';
import logger from '../utils/logger.js';

const ACCOUNT_KEY = 'bilibili_account';

export const BilibiliClientLoginMethods = {
  // 更新账号信息（返回是否登录有效）
  async updateAccountInfo() {
    const accountInfoResponse = await this.getRequest('https://api.bilibili.com/x/web-interface/nav');

    if (!accountInfoResponse || !accountInfoResponse.data || !accountInfoResponse.data.data ||
        !accountInfoResponse.data.data.isLogin) {
      logger.log('updateAccountInfo: 登录态无效');
      return false;
    }

    this.accountInfo = accountInfoResponse.data.data;
    return true;
  },

  // 刷新BUVID
  async updateBUVID() {
    const response = await this.getRequest('https://api.bilibili.com/x/frontend/finger/spi');
    if (response && response.data && response.data.data) {
      this.buvid3 = response.data.data.b_3;
      this.buvid4 = response.data.data.b_4;
    }
  },

  // 获取二维码信息
  async loginQR() {
    logger.log('请求登录二维码');
    const response = await this.getRequest(
      'https://passport.bilibili.com/x/passport-login/web/qrcode/generate'
    );
    if (response && response.data) {
      this.qrCodeKey = response.data.data.qrcode_key;
      return { url: response.data.data.url, qrcode_key: response.data.data.qrcode_key };
    }
    throw new Error('获取二维码失败');
  },

  // 登录：优先本地存储的账号数据；send_req=true 时轮询扫码结果
  async login(send_req = false) {
    const accountData = await this.getStoredAccountData();
    if (accountData) {
      this.sessData = accountData.sessData;
      this.biliJct = accountData.biliJct;
      this.dedeUserID = accountData.dedeUserID;
      this.sid = accountData.sid;
      logger.log('使用存储的账号数据登录');
      const valid = await this.updateAccountInfo();
      if (!valid) {
        return { success: false, message: '登录已失效，请重新扫码' };
      }
      await this.updateBUVID();
      return { success: true, message: '登录成功' };
    }

    if (send_req) {
      const response = await this.getRequest(
        `https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=${this.qrCodeKey}`
      );
      if (response && response.data && response.data.data) {
        const d = response.data.data;
        if (d.code === 0) {
          // 从响应头提取 Cookies（适配层键为小写）
          const setCookie =
            (response.headers && (response.headers['set-cookie'] || response.headers['Set-Cookie'])) ||
            null;
          if (setCookie) {
            this.extractCookiesFromResponse(setCookie);
            if (!this.sessData || !this.biliJct || !this.dedeUserID || !this.sid) {
              return { success: false, message: '登录失败：Set-Cookie 缺少必要字段', code: 0 };
            }
            const accountValid = await this.updateAccountInfo();
            if (!accountValid) {
              return { success: false, message: '登录失败：账号状态验证未通过', code: 0 };
            }
            await this.updateBUVID();
            await this.storeAccountData();
            logger.log('二维码登录并验证账号状态成功');
            return { success: true, message: '登录成功', code: 0 };
          }
          return { success: false, message: '登录失败：响应中没有 Set-Cookie', code: 0 };
        }
        // qrcode/poll 业务码：86101 未扫码 / 86090 已扫码待确认 / 86038 二维码过期
        const msg =
          d.code === 86101 ? '等待扫码...' :
          d.code === 86090 ? '请在手机上确认登录' :
          d.code === 86038 ? '二维码已过期' :
          '等待用户操作...';
        return { success: false, message: msg, code: d.code };
      }
      return { success: false, message: '等待用户操作...', code: -1 };
    }

    return { success: false, message: '未登录' };
  },

  // 退出登录（删除本地存储的账号数据）
  async logOut() {
    this.sessData = null;
    this.biliJct = null;
    this.dedeUserID = null;
    this.sid = null;
    this.accountInfo = null;
    await storage.del(ACCOUNT_KEY);
  },

  // 从响应头中提取Cookies
  extractCookiesFromResponse(setCookieHeaders) {
    const cookieLines = Array.isArray(setCookieHeaders) ? setCookieHeaders : [setCookieHeaders];
    cookieLines.forEach((cookie) => {
      if (typeof cookie !== 'string') return;
      const sessData = this.parseCookie(cookie, 'SESSDATA');
      const biliJct = this.parseCookie(cookie, 'bili_jct');
      const dedeUserID = this.parseCookie(cookie, 'DedeUserID');
      const sid = this.parseCookie(cookie, 'sid');
      if (sessData) this.sessData = sessData;
      if (biliJct) this.biliJct = biliJct;
      if (dedeUserID) this.dedeUserID = dedeUserID;
      if (sid) this.sid = sid;
    });
  },

  // 辅助函数，用于解析cookie字符串
  parseCookie(cookie, name) {
    const match = cookie.match(new RegExp(`(?:^|[;,]\\s*)${name}=([^;]+)`));
    return match ? match[1] : null;
  },

  // 获取本地存储的账号数据
  async getStoredAccountData() {
    try {
      const data = await storage.get(ACCOUNT_KEY);
      return data ? JSON.parse(data) : null;
    } catch (err) {
      logger.log('获取存储的账号数据失败：', err);
      return null;
    }
  },

  // 保存账号数据到本地存储
  async storeAccountData() {
    if (this.sessData && this.biliJct && this.dedeUserID && this.sid) {
      const accountData = {
        sessData: this.sessData,
        biliJct: this.biliJct,
        dedeUserID: this.dedeUserID,
        sid: this.sid
      };
      await storage.set(ACCOUNT_KEY, JSON.stringify(accountData));
      logger.log('账号数据存储成功');
    }
  }
};
