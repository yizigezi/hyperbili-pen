<template>
  <div class="page">
    <div class="topbar">
      <div class="topbtn" @click="$page.finish()">
        <image class="topicon" src="/assets/fullgray.png" />
        <text class="toptext">返回</text>
      </div>
      <text class="top-title">私信</text>
      <div class="topbtn" @click="InitSessions">
        <text class="toptext">刷新</text>
      </div>
    </div>

    <div class="loading" v-if="loading">
      <text class="loading-text">正在加载...</text>
    </div>

    <scroll class="scroll" scroll-y="true" v-else>
      <div class="user" v-for="item in viewSessions" :key="item.talker_id"
        @click="GoDMPage(item.talker_id)">
        <image class="userface"
          :src="(session_cards[item.talker_id] && session_cards[item.talker_id].face ? session_cards[item.talker_id].face + '@75w_75h' : '/assets/fullgray.png')" />
        <div class="infoarea">
          <text class="username">{{ (session_cards[item.talker_id] && session_cards[item.talker_id].name) || item.talker_id }}</text>
          <text class="lastmsg">{{ LastMsgText(item) }}</text>
        </div>
      </div>
      <div class="bottom-space"></div>
    </scroll>

    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>
  </div>
</template>

<script>
import biliclient from '../../bilibiliclient/index.js'

export default {
  name: 'dmlist',
  data() {
    return {
      sessions: [],
      session_cards: {},
      loading: true,
      toast: ''
    }
  },
  computed: {
    viewSessions() {
      return this.sessions
    }
  },
  methods: {
    showToast(msg) {
      this.toast = msg
      this.$page.setTimeout(() => { this.toast = '' }, 3000)
    },
    onShow() {
      this.InitSessions()
    },
    LastMsgText(item) {
      if (!item.last_msg || !item.last_msg.content) return '暂无消息'
      try {
        const c = JSON.parse(item.last_msg.content)
        return c.content || c.reply_content || c.text || '暂无消息'
      } catch (e) {
        return '暂无消息'
      }
    },
    async InitSessions() {
      this.loading = true
      try {
        // 页面独立启动时（含 web 预览）账号信息可能还没拉过
        if (!biliclient.accountInfo) {
          const ok = await biliclient.updateAccountInfo()
          if (!ok) {
            $falcon.navTo('index')
            return
          }
        }
        const dmData = await biliclient.getDMSessions(1, 2)
        const sessionsData = (dmData && dmData.session_list ? dmData.session_list : []).slice(0, 10)

        const session_uids = []
        const filteredSessions = []
        sessionsData.forEach((session) => {
          if (session.talker_id) {
            filteredSessions.push(session)
            session_uids.push(session.talker_id)
          }
        })
        this.sessions = filteredSessions

        if (session_uids.length > 0) {
          this.session_cards = (await biliclient.getMultiUserInfoByUID(session_uids)) || {}
        }
      } catch (e) {
        this.showToast('加载失败：' + (e && e.message ? e.message : e))
      }
      this.loading = false
    },
    GoDMPage(targetId) {
      const card = this.session_cards[`${targetId}`] || {}
      $falcon.navTo('dmpage', {
        myId: String(biliclient.accountInfo.mid),
        targetId: String(targetId),
        myPic: String(biliclient.accountInfo.face || ''),
        targetPic: String(card.face || '')
      })
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
  flex-direction: row;
  align-items: center;
  padding: 8px 14px;
}

.topicon {
  width: 22px;
  height: 22px;
}

.toptext {
  font-size: 18px;
  color: #ff7da8;
  margin-left: 6px;
}

.top-title {
  font-size: 22px;
  font-weight: bold;
  color: #ffffff;
}

.scroll {
  width: 100%;
  flex-direction: column;
  align-items: center;
}

.loading {
  margin-top: 60px;
  align-items: center;
}

.loading-text {
  font-size: 20px;
  color: #585858;
}

.user {
  margin-top: 10px;
  min-height: 100px;
  border-radius: 20px;
  background-color: #222222;
  flex-direction: row;
  width: 92%;
  align-items: center;
  padding: 10px;
}

.userface {
  width: 70px;
  height: 70px;
  object-fit: scale-down;
  margin-left: 10px;
  border-radius: 50%;
}

.infoarea {
  flex-direction: column;
  justify-content: center;
  flex: 1;
  margin-left: 14px;
}

.username {
  font-size: 22px;
  font-weight: 600;
  max-height: 36px;
  text-overflow: ellipsis;
  color: #ffffff;
}

.lastmsg {
  font-size: 16px;
  font-weight: 600;
  margin-top: 6px;
  max-height: 44px;
  text-overflow: ellipsis;
  color: #585858;
}

.bottom-space {
  height: 30px;
}
</style>
