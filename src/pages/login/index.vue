<template>
  <div class="page">
    <!-- 第 1 页：凭证密钥 -->
    <div class="screen" v-if="currentStep !== 'qr'">
      <div class="key-info">
        <text class="title">{{ currentStep === 'confirm' ? '确认凭证密钥' : '凭证密钥' }}</text>
        <text class="description">{{ currentStep === 'confirm' ? '请再次输入刚才设置的数字密钥。' : '首次使用需要设置密钥；已有凭证库请输入原密钥。' }}</text>
        <text class="description">每次启动都需要输入，忘记后无法找回。</text>
        <div class="password-display">
          <text class="password-dots">{{ vaultPassword ? '● '.repeat(vaultPassword.length) : (currentStep === 'confirm' ? '再次输入 8–32 位数字' : '输入 8–32 位数字') }}</text>
          <text class="password-count">{{ vaultPassword.length }}/32</text>
        </div>
        <text class="status">{{ status }}</text>
        <div class="login-actions">
          <div class="primary-button" @click="UnlockVault">
            <text class="button-text">{{ currentStep === 'confirm' ? '确认密钥并继续' : '确认' }}</text>
          </div>
          <div class="preview-button" @click="PreviewDM">
            <text class="button-text">测试私聊界面</text>
          </div>
        </div>
      </div>

      <div class="keypad">
        <div class="key-row">
          <div class="number-key" @click="AppendDigit('1')"><text class="number-key-text">1</text></div>
          <div class="number-key" @click="AppendDigit('2')"><text class="number-key-text">2</text></div>
          <div class="number-key" @click="AppendDigit('3')"><text class="number-key-text">3</text></div>
        </div>
        <div class="key-row">
          <div class="number-key" @click="AppendDigit('4')"><text class="number-key-text">4</text></div>
          <div class="number-key" @click="AppendDigit('5')"><text class="number-key-text">5</text></div>
          <div class="number-key" @click="AppendDigit('6')"><text class="number-key-text">6</text></div>
        </div>
        <div class="key-row">
          <div class="number-key" @click="AppendDigit('7')"><text class="number-key-text">7</text></div>
          <div class="number-key" @click="AppendDigit('8')"><text class="number-key-text">8</text></div>
          <div class="number-key" @click="AppendDigit('9')"><text class="number-key-text">9</text></div>
        </div>
        <div class="key-row">
          <div class="number-key number-key-action" @click="ClearPassword"><text class="number-key-small">清空</text></div>
          <div class="number-key" @click="AppendDigit('0')"><text class="number-key-text">0</text></div>
          <div class="number-key number-key-action" @click="Backspace"><text class="number-key-small">退格</text></div>
        </div>
      </div>
    </div>

    <!-- 第 2 页：二维码登录 -->
    <div class="screen qr-screen" v-else>
      <div class="qr-card">
        <FlQrcode v-if="show_qr" :text="qr_value" :size="184" />
        <text v-else class="qr-placeholder">{{ qrLoading ? '二维码加载中…' : (qrError || '二维码暂不可用') }}</text>
      </div>
      <div class="qr-info">
        <text class="title">扫码登录哔哩哔哩</text>
        <text class="description">{{ hint }}</text>
        <text class="status">{{ status }}</text>
        <text class="key-ready">凭证密钥已设置 · {{ vaultPassword.length }} 位</text>
      <div class="response-panel" v-if="qrResponse">
        <text class="response-title">扫码接口返回内容</text>
        <scroll class="response-scroll" scroll-y="true">
          <text class="response-content">{{ qrResponse }}</text>
        </scroll>
      </div>
        <div class="actions">
          <div class="secondary-button" @click="BackToKey"><text class="button-text">返回</text></div>
          <div class="primary-button action-primary" v-if="show_retry" @click="GetQR"><text class="button-text">刷新二维码</text></div>
        </div>
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
      currentStep: 'key',
      qr_value: '',
      show_qr: false,
      show_retry: false,
      qrLoading: false,
      qrError: '',
      status: '请输入凭证库密钥。',
      hint: '请使用手机哔哩哔哩扫描二维码',
      vaultPassword: '',
      firstPassword: '',
      toast: '',
      pollToken: null,
      polling: false,
      qrResponse: ''
    }
  },
  methods: {
    showToast(msg) {
      this.toast = msg
      this.$page.setTimeout(() => { this.toast = '' }, 3000)
    },
    onShow() {
      this.status = '请输入凭证库密钥。'
    },
    onDestroy() {
      this.StopPolling()
    },
    PreviewDM() {
      $falcon.navTo('dmpage', { preview: true })
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
    StopPolling() {
      if (this.pollToken !== null) {
        this.$page.clearTimer(this.pollToken)
        this.pollToken = null
      }
      this.polling = false
    },
    BackToKey() {
      this.StopPolling()
      this.currentStep = 'key'
      this.show_qr = false
      this.show_retry = false
      this.firstPassword = ''
      this.currentStep = 'key'
      this.status = '请输入凭证库密钥。'
      this.hint = '请使用手机哔哩哔哩扫描二维码'
    },
    async UnlockVault() {
      if (this.vaultPassword.length < 8) {
        this.showToast('数字密钥至少需要 8 位')
        return
      }

      if (this.currentStep === 'confirm') {
        if (this.vaultPassword !== this.firstPassword) {
          this.vaultPassword = ''
          this.status = '两次输入不一致，请重新确认。'
          this.showToast('两次密钥不一致')
          return
        }
        this.firstPassword = ''
        await this.GetQR()
        return
      }

      this.status = '正在检查凭证库…'
      try {
        const exists = await biliclient.hasCredentialVault()
        if (!exists) {
          this.firstPassword = this.vaultPassword
          this.vaultPassword = ''
          this.currentStep = 'confirm'
          this.status = '请再次输入密钥以确认。'
          return
        }

        this.status = '正在解锁凭证库…'
        const result = await biliclient.login(false, this.vaultPassword)
        if (result && result.success) {
          this.vaultPassword = ''
          $falcon.navTo('dmlist')
          return
        }
        this.status = '凭证库中没有可用账号，请重新登录。'
        await this.GetQR()
      } catch (error) {
        this.status = '解锁失败，请检查密钥是否正确。'
        this.showToast(error && error.message ? error.message : String(error))
      }
    },
    async GetQR() {
      this.StopPolling()
      this.currentStep = 'qr'
      this.show_retry = false
      this.show_qr = false
      this.qrLoading = true
      this.qrError = ''
      this.status = '正在获取登录二维码…'
      try {
        const result = await biliclient.loginQR()
        this.qr_value = result.url
        this.show_qr = true
        this.qrLoading = false
        this.hint = '打开手机哔哩哔哩，扫描左侧二维码并确认登录。'
        this.polling = true
        this.pollToken = this.$page.setInterval(() => this.PollQR(), POLL_MS)
        this.status = '等待扫码…'
      } catch (error) {
        this.qrLoading = false
        const message = error && error.message ? error.message : String(error)
        this.qrError = message
        this.status = '二维码获取失败：' + message
        this.show_retry = true
        this.showToast(message)
      }
    },
    async PollQR() {
      if (!this.polling) return
      try {
        const result = await biliclient.login(true, this.vaultPassword)
        if (result.responsePreview) this.qrResponse = result.responsePreview
        if (result.success) {
          this.StopPolling()
          this.vaultPassword = ''
          this.status = '扫码登录成功，响应内容已显示。'
        } else {
          this.hint = result.message || '等待扫码…'
          if (result.code === 86038) {
            this.StopPolling()
            this.status = '二维码已过期，请刷新。'
            this.show_retry = true
          }
        }
      } catch (error) {
        this.StopPolling()
        this.status = '登录或加密保存失败，请检查后重试。'
        this.show_retry = true
        this.showToast(error && error.message ? error.message : String(error))
      }
    }
  }
}
</script>

<style lang="less">
@import '../../styles/hyper.less';

.page {
  width: 100%;
  height: 100%;
  padding: 0;
  justify-content: center;
}

.screen {
  width: 100%;
  height: 100%;
  padding: 10px 16px;
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
}

.key-info {
  width: 47%;
  height: 100%;
  flex-direction: column;
  justify-content: center;
  align-items: flex-start;
  padding-right: 12px;
}

.title {
  font-size: 22px;
  font-weight: bold;
  color: #ffffff;
}

.description {
  margin-top: 6px;
  font-size: 14px;
  color: #dddddd;
}

.password-display {
  width: 100%;
  height: 36px;
  margin-top: 10px;
  padding: 6px 10px;
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
  background-color: #ffffff;
  border-radius: 6px;
}

.password-dots {
  font-size: 16px;
  color: #222222;
}

.password-count {
  font-size: 13px;
  color: #777777;
}

.status {
  margin-top: 7px;
  font-size: 13px;
  color: #ffb4ce;
}

.login-actions {
  flex-direction: row;
  align-items: center;
  margin-top: 2px;
}

.preview-button {
  height: 34px;
  min-width: 124px;
  margin-top: 9px;
  margin-left: 8px;
  padding: 0 10px;
  align-items: center;
  justify-content: center;
  border-radius: 6px;
  background-color: #50505a;
}

.primary-button {
  height: 34px;
  min-width: 150px;
  margin-top: 9px;
  padding: 0 14px;
  align-items: center;
  justify-content: center;
  border-radius: 6px;
  background-color: #ff7da8;
}

.button-text {
  font-size: 14px;
  font-weight: 600;
  color: #ffffff;
}

.keypad {
  width: 49%;
  height: 100%;
  flex-direction: column;
  justify-content: center;
  padding-left: 8px;
}

.key-row {
  width: 100%;
  height: 37px;
  margin-top: 3px;
  flex-direction: row;
  justify-content: space-between;
}

.number-key {
  width: 32%;
  height: 34px;
  align-items: center;
  justify-content: center;
  border-radius: 6px;
  background-color: #34343b;
}

.number-key-action {
  background-color: #50505a;
}

.number-key-text {
  font-size: 19px;
  color: #ffffff;
}

.number-key-small {
  font-size: 13px;
  color: #ffffff;
}

.qr-screen {
  justify-content: flex-start;
}

.qr-card {
  width: 204px;
  height: 204px;
  flex-shrink: 0;
  align-items: center;
  justify-content: center;
  border-radius: 8px;
  background-color: #ffffff;
}

.qr-placeholder {
  font-size: 14px;
  color: #585858;
}

.response-panel {\n  width: 100%;\n  height: 118px;\n  margin-top: 8px;\n  padding: 6px;\n  background-color: #24242b;\n  border-radius: 6px;\n  flex-direction: column;\n}\n\n.response-title {\n  font-size: 12px;\n  color: #ffb4ce;\n}\n\n.response-scroll {\n  width: 100%;\n  flex: 1;\n  margin-top: 4px;\n}\n\n.response-content {\n  font-size: 10px;\n  color: #eeeeee;\n}\n\n.qr-info {
  flex: 1;
  height: 100%;
  margin-left: 22px;
  flex-direction: column;
  justify-content: center;
  align-items: flex-start;
}

.key-ready {
  margin-top: 8px;
  font-size: 12px;
  color: #9c9ca5;
}

.actions {
  width: 100%;
  margin-top: 10px;
  flex-direction: row;
  align-items: center;
}

.secondary-button {
  height: 32px;
  min-width: 76px;
  padding: 0 14px;
  align-items: center;
  justify-content: center;
  border-radius: 6px;
  background-color: #50505a;
}

.action-primary {
  min-width: 112px;
  height: 32px;
  margin-top: 0;
  margin-left: 10px;
}

.toast {
  left: 16px;
  right: 16px;
  bottom: 8px;
  padding: 7px 10px;
}

.toast-text {
  font-size: 13px;
}
</style>
