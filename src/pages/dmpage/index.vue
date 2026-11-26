<template>
  <div class="page">
    <div class="topbar">
      <div class="topbtn" @click="$page.finish()">
        <text class="toptext">返回</text>
      </div>
      <text class="top-title">私信详情</text>
      <div class="topbtn" @click="UpdateMessages()">
        <text class="toptext">刷新</text>
      </div>
    </div>

    <div class="loading" v-if="loading">
      <text class="loading-text">正在加载...</text>
    </div>

    <scroll class="scroll" scroll-y="true" v-else>
      <div v-for="item in messages" :key="item.msg_seqno">
        <!-- 对方的消息 -->
        <div class="targetmsg-container" v-if="item.receiver_id == myId">
          <div class="targetmsg">
            <image class="msgprofpic"
              :src="targetPic ? targetPic + '@75w_75h' : '/assets/fullgray.png'" />
            <div class="msgbox">
              <text class="msgtext" v-if="item.msg_type == 1">{{ ParseContent(item.content) }}</text>
              <text class="msgtext" v-else-if="item.msg_type == 5">[已撤回] {{ ParseContent(item.content) }}</text>
              <image class="imgmsg" v-else-if="item.msg_type == 2"
                :src="ParseContent(item.content, 'url')" />
              <text class="msgtext" v-else>[不支持的消息类型]</text>
            </div>
          </div>
        </div>
        <!-- 我发出的消息 -->
        <div class="mymsg-container" v-else-if="item.receiver_id == targetId">
          <div class="mymsg">
            <div class="msgmine">
              <text class="msgtext" v-if="item.msg_type == 1">{{ ParseContent(item.content) }}</text>
              <text class="msgtext" v-else-if="item.msg_type == 5">[已撤回] {{ ParseContent(item.content) }}</text>
              <image class="imgmsg" v-else-if="item.msg_type == 2"
                :src="ParseContent(item.content, 'url')" />
              <text class="msgtext" v-else>[不支持的消息类型]</text>
            </div>
            <image class="msgprofpic"
              :src="myPic ? myPic + '@75w_75h' : '/assets/fullgray.png'" />
          </div>
        </div>
      </div>
      <div class="bottom-space"></div>
    </scroll>

    <div class="sendbox">
      <div class="sendinput" @click="OpenSystemInput">
        <text class="sendinput-text" :class="{ 'sendinput-placeholder': !input_text }">
          {{ input_text || '点击输入私信内容' }}
        </text>
      </div>
      <div class="sendbtn" @click="SendMsg">
        <text class="sendbtn-text">发送</text>
      </div>
    </div>

    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>
  </div>
</template>

<script>
import biliclient from '../../bilibiliclient/index.js'

const REFRESH_MS = 3000

export default {
  name: 'dmpage',
  data() {
    return {
      myId: 0,
      targetId: 0,
      myPic: '',
      targetPic: '',
      preview: false,
      messages: [],
      input_text: '',
      loading: true,
      toast: '',
      refreshToken: null
    }
  },
  methods: {
    showToast(msg) {
      this.toast = msg
      this.$page.setTimeout(() => { this.toast = '' }, 3000)
    },
    onShow() {
      this.applyOptions()
      this.$page.clearTimer(this.refreshToken)
      if (this.preview) {
        this.loading = false
        return
      }
      this.UpdateMessages()
      this.refreshToken = this.$page.setInterval(() => this.UpdateMessages(), REFRESH_MS)
    },
    onLoad(opts) {
      this.applyOptions(opts)
      if (this.preview) this.SetPreviewMessages()
    },
    onNewOptions(opts) {
      const oldTarget = this.targetId
      this.applyOptions(opts)
      if (this.targetId !== oldTarget) {
        this.UpdateMessages()
      }
    },
    applyOptions(opts) {
      const o = opts || (this.$page && this.$page.options) || {}
      if (o.myId !== undefined) this.myId = o.myId
      if (o.targetId !== undefined) this.targetId = o.targetId
      if (o.myPic !== undefined) this.myPic = o.myPic
      if (o.targetPic !== undefined) this.targetPic = o.targetPic
      this.preview = o.preview === true || o.preview === 'true'
    },
    SetPreviewMessages() {
      this.myId = 1
      this.targetId = 2
      this.targetPic = ''
      this.myPic = ''
      this.messages = [
        {
          msg_seqno: 1001,
          receiver_id: this.myId,
          msg_type: 1,
          content: JSON.stringify({ content: '你好！这是私聊页面的预览消息。' })
        },
        {
          msg_seqno: 1002,
          receiver_id: this.targetId,
          msg_type: 1,
          content: JSON.stringify({ content: '底部输入框可以点开试试系统输入法。' })
        },
        {
          msg_seqno: 1003,
          receiver_id: this.myId,
          msg_type: 1,
          content: JSON.stringify({ content: '预览模式不会请求真实私信，也不会发送消息。' })
        }
      ]
      this.loading = false
    },
    onDestroy() {
      this.$page.clearTimer(this.refreshToken)
    },
    ParseContent(content, field = 'content') {
      try {
        const c = JSON.parse(content)
        return c[field] || ''
      } catch (e) {
        return ''
      }
    },
    OpenSystemInput() {
      const falcon = (typeof $falcon !== 'undefined' && $falcon) ||
        (typeof window !== 'undefined' && window.$falcon)
      if (!falcon || typeof falcon.showSoftInput !== 'function') {
        this.showToast('当前固件未提供系统输入法接口')
        return
      }
      try {
        falcon.showSoftInput({
          type: 'text',
          placeholder: '输入私信内容',
          onConfirm: (value) => {
            // Falcon 输入法回调通常直接返回文本；兼容对象形式的回传值。
            const text = typeof value === 'string'
              ? value
              : (value && (value.text || value.value || value.content)) || ''
            this.input_text = String(text)
          }
        })
      } catch (error) {
        this.showToast('调用系统输入法失败：' + (error && error.message ? error.message : error))
      }
    },
    async SendMsg() {
      if (this.preview) {
        this.showToast('预览模式不发送真实消息')
        return
      }
      const text = String(this.input_text || '').trim()
      if (!text) return
      try {
        const result = await biliclient.SendDMSessionMessage(String(this.targetId), 1, text)
        if (!result || result.code !== 0) {
          this.showToast(`发送失败！错误代码：${result ? result.code : '网络错误'}${result && result.message ? ' ' + result.message : ''}`)
        } else {
          this.input_text = ''
          this.UpdateMessages()
        }
      } catch (error) {
        this.showToast('发送失败！' + (error && error.message ? error.message : error))
      }
    },
    async UpdateMessages() {
      try {
        // 页面独立启动时（含 web 预览）账号信息可能还没拉过
        if (!biliclient.accountInfo) {
          const ok = await biliclient.updateAccountInfo()
          if (!ok) {
            $falcon.navTo('index')
            return
          }
        }
        const response = await biliclient.getDMSessionMessage(1, String(this.targetId), 30)
        if (response && response.messages) {
          const newMessages = response.messages.slice().reverse()
          const lastOld = this.messages[this.messages.length - 1]
          if (
            newMessages.length > 0 &&
            (!lastOld || newMessages[newMessages.length - 1].msg_seqno !== lastOld.msg_seqno)
          ) {
            this.messages = newMessages
            // TODO: 滚到底部（$element/scrollTo 在 Falcon 上待验证）
          }
        }
      } catch (error) {
        this.showToast('更新消息失败！' + (error && error.message ? error.message : error))
      }
      this.loading = false
    }
  }
}
</script>

<style lang="less">
@import '../../styles/hyper.less';

.topbar {
  width: 100%;
  height: 60px;
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
  padding: 0 15px;
}

.topbtn {
  padding: 8px 14px;
}

.toptext {
  font-size: 18px;
  color: #ff7da8;
}

.top-title {
  font-size: 22px;
  font-weight: bold;
  color: #ffffff;
}

.loading {
  margin-top: 60px;
  align-items: center;
}

.loading-text {
  font-size: 20px;
  color: #585858;
}

.scroll {
  width: 100%;
  flex-direction: column;
}

.targetmsg-container {
  flex-direction: row;
  justify-content: flex-start;
  width: 100%;
  margin-top: 15px;
  padding: 0 20px;
}

.mymsg-container {
  flex-direction: row;
  justify-content: flex-end;
  width: 100%;
  margin-top: 15px;
  padding: 0 20px;
}

.targetmsg {
  flex-direction: row;
  align-items: flex-start;
  max-width: 85%;
}

.mymsg {
  flex-direction: row;
  align-items: flex-start;
  max-width: 85%;
}

.msgbox {
  max-width: 240px;
  border-radius: 16px;
  padding: 12px;
  background-color: #242424;
  justify-content: center;
}

.msgmine {
  max-width: 240px;
  border-radius: 16px;
  padding: 12px;
  background-color: #ff7da8;
  justify-content: center;
}

.msgprofpic {
  width: 44px;
  height: 44px;
  border-radius: 50%;
  object-fit: scale-down;
  margin: 0 6px;
}

.imgmsg {
  object-fit: scale-down;
  border-radius: 12px;
  max-width: 180px;
  max-height: 150px;
}

.msgtext {
  font-size: 18px;
  font-weight: 600;
  color: #ffffff;
  text-overflow: ellipsis;
  max-height: 130px;
}

.sendbox {
  width: 100%;
  flex-direction: row;
  align-items: center;
  padding: 10px 15px;
  background-color: #1c1c1c;
}

.sendinput {
  flex: 1;
  height: 46px;
  border-radius: 23px;
  background-color: #242424;
  padding: 0 18px;
  justify-content: center;
}

.sendinput-text {
  font-size: 18px;
  color: #ffffff;
  max-height: 30px;
  text-overflow: ellipsis;
}

.sendinput-placeholder {
  color: #888888;
}

.sendbtn {
  margin-left: 12px;
  padding: 12px 22px;
  border-radius: 20px;
  background-color: #ff7da8;
}

.sendbtn-text {
  font-size: 18px;
  font-weight: 600;
  color: #ffffff;
}

.bottom-space {
  height: 30px;
}
</style>
