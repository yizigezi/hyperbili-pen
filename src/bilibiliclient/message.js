/**
 * 私信相关方法（挂到 BiliClient.prototype 的 mixin）。
 * 移植自 HyperBilibili (GPL-3.0) src/bilibiliclient/message/message.ts。
 */

export const BilibiliClientMessageMethods = {
  // 获取通知信息数量（回复我的、at我的、点赞数量）
  async getMessageNotifyFeed() {
    const url = 'https://api.vc.bilibili.com/x/im/web/msgfeed/unread';
    const response = await this.getRequest(url);
    return response.data.data;
  },

  // 获取私信Session列表
  // 一次性最多拉取20个
  async getDMSessions(session_type, sort_rule) {
    const url = 'https://api.vc.bilibili.com/session_svr/v1/session_svr/get_sessions';
    const response = await this.getRequestWbi(url, {
      session_type,
      sort_rule,
      group_fold: 0,
      unfollow_fold: 0,
      mobi_app: 'web'
    });

    return response.data.data;
  },

  // 获取私信Session聊天记录
  // 若要做增量拉取，end_seqno 应为最顶上那条信息的序列号
  async getDMSessionMessage(session_type, talker_id, size = 10, end_seqno = '') {
    const url = 'https://api.vc.bilibili.com/svr_sync/v1/svr_sync/fetch_session_msgs';
    // 注意：不要写成「字面量 + 后续成员赋值」，uglify-es 的 join_object_assignments 会崩
    const params = end_seqno
      ? { session_type, talker_id, size, end_seqno }
      : { session_type, talker_id, size };
    const response = await this.getRequestWbi(url, params);

    return response.data.data;
  },

  // 发送私信消息
  async SendDMSessionMessage(receiver_id, msg_type, content) {
    const url = 'https://api.vc.bilibili.com/web_im/v1/web_im/send_msg';
    const body = `msg[sender_uid]=${this.accountInfo.mid}&msg[receiver_id]=${receiver_id}&msg[receiver_type]=1&msg[msg_type]=${msg_type}&msg[dev_id]=${this.dm_deviceid}&msg[timestamp]=${Number.parseInt(
      (new Date().getTime() / 1000).toString()
    )}&msg[content]=${encodeURIComponent(JSON.stringify({ content: `${content}` }))}&csrf=${this.biliJct}&csrf_token=${this.biliJct}&msg[msg_status]=0&msg[new_face_version]=0&from_firework=0&build=0&mobi_app=web`;

    // 同上：字面量 + 成员赋值会被 uglify-es 搞崩，一次性展开
    const headers = {
      ...this.getHeaders(),
      'Content-Type': 'application/x-www-form-urlencoded',
      'Host': 'api.vc.bilibili.com',
      'Origin': 'https://message.bilibili.com',
      'Referer': 'https://message.bilibili.com/'
    };
    // Content-Length 由网络层自动带，这里不再手算（原版按字节数算，适配层不需要）

    const response = await this.postRequestWbi(
      url,
      {
        w_sender_uid: this.accountInfo.mid,
        w_receiver_id: receiver_id,
        w_dev_id: this.dm_deviceid
      },
      body,
      'application/x-www-form-urlencoded',
      headers
    );

    return response.data;
  }
};
