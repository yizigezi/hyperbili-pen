/**
 * 用户信息相关方法（挂到 BiliClient.prototype 的 mixin）。
 * 移植自 HyperBilibili (GPL-3.0) src/bilibiliclient/user/user.ts（取私信用到的那一个方法）。
 */

export const BilibiliClientUserMethods = {
  // 批量获取用户名片信息（返回以 uid 为键的映射）
  async getMultiUserInfoByUID(uids) {
    const url = 'https://api.bilibili.com/x/polymer/pc-electron/v1/user/cards';
    let param = '';
    uids.forEach((uid) => {
      if (uid.toString().length > 0) {
        param += uid;
        if (uids.indexOf(uid) != uids.length - 1) {
          param += ',';
        }
      }
    });
    const response = await this.getRequest(`${url}?uids=${param}`);

    return response.data.data;
  }
};
