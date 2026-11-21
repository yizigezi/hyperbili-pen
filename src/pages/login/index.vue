<template>
  <div class="page">
    <div class="login-content">
      <div class="login-info">
        <text class="title">扫码登录哔哩哔哩</text>
        <text class="hint">{{ hint }}</text>
        <text class="status">首次使用请设置密码；以后每次启动都需要输入。密码无法找回。</text>
        <text v-if="!show_qr" class="status">{{ status }}</text>
        <text class="keypad-label">凭证密钥（数字，至少 8 位）</text>
        <div class="password-display">
          <text class="password-dots">{{ vaultPassword ? '● '.repeat(vaultPassword.length) : '请输入数字密钥' }}</text>
          <text class="password-count">{{ vaultPassword.length }} 位</text>
        </div>
        <div class="number-keypad">
          <div class="number-key" v-for="n in ['1','2','3','4','5','6','7','8','9']" :key="n" @click="AppendDigit(n)">
            <text class="number-key-text">{{ n }}</text>
          </div>
          <div class="number-key number-key-action" @click="ClearPassword"><text class="number-key-text">清空</text></div>
          <div class="number-key" @click="AppendDigit('0')"><text class="number-key-text">0</text></div>
          <div class="number-key number-key-action" @click="Backspace"><text class="number-key-text">⌫</text></div>
        </div>
        <div class="retry vault-action" @click="UnlockVault"><text class="retry-text">解锁并继续</text></div>
        <div class="retry" v-if="show_retry" @click="GetQR">
          <text class="retry-text">刷新二维码</text>
        </div>
      </div>

      <div class="qr-card">
        <FlQrcode v-if="show_qr" :text="qr_value" :size="180" />
        <text v-else class="qr-placeholder">二维码加载中</text>
      </div>
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
      vaultPassword: '',
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
      this.status = '请输入凭证库密码后继续';
    },
    onDestroy() {
      this.$page.clearTimer(this.pollToken)
    },
    AppendDigit(digit) {
      if (this.vaultPassword.length >= 32) return
      this.vaultPassword += digit
    },
    Backspace() {
      this.vaultPassword = this.vaultPassword.slice(0, -1)
    },
    ClearPassword() {
      this.vaultPassword = ''
    },
    async UnlockVault() {
      if (!this.vaultPassword || this.vaultPassword.length < 8) {
        this.showToast('密码至少需要 8 位')
        return
      }
      this.status = '正在解锁凭证库...'
      try {
        const result = await biliclient.login(false, this.vaultPassword)
        if (result && result.success) {
          this.vaultPassword = ''
          $falcon.navTo('dmlist')
          return
        }
        this.status = '未发现已保存账号，请扫码登录'
        this.GetQR()
      } catch (error) {
        this.status = '凭证库解锁失败'
        this.showToast(error && error.message ? error.message : String(error))
      }
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
        const result = await biliclient.login(true, this.vaultPassword)
        if (result.success) {
          this.$page.clearTimer(this.pollToken)
          this.vaultPassword = ''
          $falcon.navTo('dmlist')
        } else {
          this.hint = result.message
          if (result.code === 86038) {
            this.$page.clearTimer(this.pollToken)
            this.show_retry = true
          }
        }
      } catch (error) {
        this.hint = '登录或加密保存失败：' + (error && error.message ? error.message : String(error))
      }
    }
  }
}
</script>

<style lang="less">
@import '../../styles/hyper.less';

.login-content {
  flex: 1;
  width: 100%;
  flex-direction: row;
  align-items: center;
  justify-content: center;
  padding: 16px 24px;
}

.login-info {
  flex: 1;
  flex-direction: column;
  justify-content: center;
  align-items: flex-start;
  padding-right: 18px;
}

.title {
  font-size: 26px;
  font-weight: bold;
  color: #ffffff;
}

.hint {
  margin-top: 18px;
  font-size: 20px;
  color: #ffffff;
}

.status {
  margin-top: 14px;
  font-size: 18px;
  color: #aaaaaa;
}

.keypad-label {
  margin-top: 18px;
  font-size: 16px;
  color: #dddddd;
}
.password-display {
  width: 100%;
  min-height: 42px;
  margin-top: 8px;
  padding: 10px 12px;
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
  background-color: #ffffff;
  border-radius: 8px;
}
.password-dots {
  font-size: 18px;
  color: #222222;
}
.password-count {
  font-size: 14px;
  color: #777777;
}
.number-keypad {
  width: 100%;
  margin-top: 10px;
  flex-direction: row;
  flex-wrap: wrap;
  justify-content: space-between;
}
.number-key {
  width: 31%;
  height: 42px;
  margin-bottom: 6px;
  align-items: center;
  justify-content: center;
  border-radius: 7px;
  background-color: #34343b;
}
.number-key-action {
  background-color: #50505a;
}
.number-key-text {
  font-size: 20px;
  color: #ffffff;
}
.vault-action { margin-top: 8px; }

.qr-card {
  width: 200px;
  height: 200px;
  flex-shrink: 0;
  align-items: center;
  justify-content: center;
  border-radius: 12px;
  background-color: #ffffff;
}

.qr-placeholder {
  font-size: 16px;
  color: #585858;
}

.retry {
  margin-top: 20px;
  padding: 12px 24px;
  border-radius: 20px;
  background-color: #ff7da8;
}

.retry-text {
  font-size: 18px;
  font-weight: 600;
  color: #ffffff;
}
</style>
