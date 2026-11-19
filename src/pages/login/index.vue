<template>
  <div class="page">
    <text class="title">扫码登录哔哩哔哩</text>
    <div class="qr-card">
      <FlQrcode v-if="show_qr" :text="qr_value" :size="180" />
      <text v-else class="status">{{ status }}</text>
    </div>
    <text class="hint">{{ hint }}</text>
    <div class="retry" v-if="show_retry" @click="GetQR">
      <text class="retry-text">刷新二维码</text>
    </div>
    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>
  </div>
</template>

<script>
import FlQrcode from 'falcon-ui/src/packages/qr-code/index.vue'
import biliclient from '../../bilibiliclient/index.js'

const POLL_MS = 1500

export default {
  name: 'login',
  components: { FlQrcode },
  data() {
    return {
      qr_value: '',
      show_qr: false,
      show_retry: false,
      status: '正在获取二维码...',
      hint: '打开手机哔哩哔哩扫码登录',
      toast: '',
      pollToken: null
    }
  },
  methods: {
    showToast(msg) {
      this.toast = msg
      this.$page.setTimeout(() => { this.toast = '' }, 3000)
    },
    async onShow() {
      const stored = await this.TryStoredLogin()
      if (!stored) {
        this.GetQR()
      }
    },
    onDestroy() {
      this.$page.clearTimer(this.pollToken)
    },
    async TryStoredLogin() {
      try {
        const result = await biliclient.login(false)
        if (result && result.success) {
          $falcon.navTo('dmlist')
          return true
        }
      } catch (e) {}
      return false
    },
    async GetQR() {
      this.show_retry = false
      this.show_qr = false
      this.status = '正在获取二维码...'
      try {
        const get_result = await biliclient.loginQR()
        this.qr_value = get_result.url
        this.show_qr = true
        this.hint = '打开手机哔哩哔哩扫码登录'
        this.$page.clearTimer(this.pollToken)
        this.pollToken = this.$page.setInterval(() => this.PollQR(), POLL_MS)
      } catch (error) {
        this.status = '二维码获取失败'
        this.show_retry = true
      }
    },
    async PollQR() {
      try {
        const result = await biliclient.login(true)
        if (result.success) {
          this.$page.clearTimer(this.pollToken)
          $falcon.navTo('dmlist')
        } else {
          this.hint = result.message
          if (result.code === 86038) {
            // 二维码过期
            this.$page.clearTimer(this.pollToken)
            this.show_retry = true
          }
        }
      } catch (error) {}
    }
  }
}
</script>

<style lang="less">
@import '../../styles/hyper.less';

.title {
  margin-top: 40px;
  font-size: 26px;
  font-weight: bold;
  color: #ffffff;
}

.qr-card {
  margin-top: 30px;
  width: 210px;
  height: 210px;
  align-items: center;
  justify-content: center;
  border-radius: 12px;
  background-color: #ffffff;
}

.status {
  font-size: 20px;
  color: #585858;
}

.hint {
  margin-top: 20px;
  font-size: 20px;
  color: #ffffff;
}

.retry {
  margin-top: 25px;
  padding: 12px 30px;
  border-radius: 20px;
  background-color: #ff7da8;
}

.retry-text {
  font-size: 20px;
  font-weight: 600;
  color: #ffffff;
}
</style>
