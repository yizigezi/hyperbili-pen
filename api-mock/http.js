/**
 * build:web 预览用 http mock（模块名 'http'，utils/http.js 第二级降级会 import('http')）。
 * 返回形状与 utils/http.js 的解析约定一致：{status, headers, result}。
 *
 * 模拟 B 站接口：
 *   - nav / finger/spi / qrcode generate：固定数据
 *   - qrcode poll：前 3 次返回 86101 等待扫码，第 4 次模拟登录成功并带 Set-Cookie
 *   - get_sessions / fetch_session_msgs / send_msg / user/cards：内置假会话
 */

let pollCount = 0;

const FAKE_USERS = {
  '10001': { name: '虚拟好友A', face: 'https://i2.hdslb.com/bfs/face/member/noface.jpg' },
  '10002': { name: '虚拟好友B', face: 'https://i2.hdslb.com/bfs/face/member/noface.jpg' }
};

const FAKE_MESSAGES = [
  { msg_seqno: '1', sender_id: '10001', receiver_id: '99999', msg_type: 1, content: JSON.stringify({ content: '你好呀，这是模拟私信' }) },
  { msg_seqno: '2', sender_id: '99999', receiver_id: '10001', msg_type: 1, content: JSON.stringify({ content: '收到！这是词典笔上的澎湃哔哩' }) },
  { msg_seqno: '3', sender_id: '10001', receiver_id: '99999', msg_type: 1, content: JSON.stringify({ content: '看起来不错喵' }) }
];

function reply(data, headers) {
  return {
    status: 200,
    headers: headers || {},
    result: JSON.stringify(data)
  };
}

export default {
  async request(opts) {
    const url = String(opts.url || '');

    if (url.includes('/x/web-interface/nav')) {
      return reply({
        code: 0,
        data: {
          isLogin: true,
          mid: 99999,
          uname: '词典笔用户',
          face: 'https://i2.hdslb.com/bfs/face/member/noface.jpg',
          wbi_img: {
            img_url: 'https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png',
            sub_url: 'https://i0.hdslb.com/bfs/wbi/4932caff0ff746eab6f01bf08b70ac45.png'
          }
        }
      });
    }

    if (url.includes('/x/frontend/finger/spi')) {
      return reply({ code: 0, data: { b_3: 'MOCKBUVID3', b_4: 'MOCKBUVID4' } });
    }

    if (url.includes('/qrcode/generate')) {
      return reply({
        code: 0,
        data: {
          url: 'https://passport.bilibili.com/h5-app/passport/login/scan?navhide=1&qrcode_key=MOCKKEY',
          qrcode_key: 'MOCKKEY'
        }
      });
    }

    if (url.includes('/qrcode/poll')) {
      pollCount += 1;
      if (pollCount < 4) {
        return reply({ code: 0, data: { code: 86101, message: 'waiting for scan', url: '' } });
      }
      pollCount = 0;
      return reply(
        { code: 0, data: { code: 0, message: '0', url: '', refresh_token: 'MOCKTOKEN', timestamp: Date.now() } },
        {
          'Set-Cookie': [
            'SESSDATA=MOCKSESSDATA; Path=/; HttpOnly',
            'bili_jct=MOCKJCT; Path=/',
            'DedeUserID=99999; Path=/',
            'sid=MOCKSID; Path=/'
          ]
        }
      );
    }

    if (url.includes('get_sessions')) {
      return reply({
        code: 0,
        data: {
          session_list: [
            {
              talker_id: 10001,
              session_ts: Date.now() / 1000,
              unread_count: 1,
              last_msg: { content: JSON.stringify({ content: '你好呀，这是模拟私信' }) }
            },
            {
              talker_id: 10002,
              session_ts: Date.now() / 1000,
              unread_count: 0,
              last_msg: { content: JSON.stringify({ text: '第二条模拟会话' }) }
            }
          ]
        }
      });
    }

    if (url.includes('fetch_session_msgs')) {
      // 真实接口按新→旧返回
      return reply({ code: 0, data: { messages: FAKE_MESSAGES.slice().reverse() } });
    }

    if (url.includes('send_msg')) {
      return reply({ code: 0, data: { msg_key: 'MOCKMSGKEY' }, message: '0' });
    }

    if (url.includes('user/cards')) {
      return reply({ code: 0, data: FAKE_USERS });
    }

    return reply({ code: -404, message: 'mock: 未知接口 ' + url, data: null });
  }
};
