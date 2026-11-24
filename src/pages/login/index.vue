<template>
  <div class="page">
    <!-- 第 1 页：凭证密钥 -->
    <div class="screen" v-if="currentStep === 'key'">
      <div class="key-info">
        <text class="title">设置凭证密钥</text>
        <text class="description">首次使用请设置数字密钥；已有账号请输入原密钥。</text>
        <text class="description">每次启动都需要输入，忘记后无法找回。</text>
        <div class="password-display">
          <text class="password-dots">{{ vaultPassword ? '● '.repeat(vaultPassword.length) : '输入 8–32 位数字' }}</text>
          <text class="password-count">{{ vaultPassword.length }}/32</text>
        </div>
        <text class="status">{{ status }}</text>
        <div class="login-actions">
          <div class="primary-button" @click="UnlockVault">
            <text class="button-text">确认并进入扫码页</text>
          </div>
          <div class="secondary-button network-test-button" @click="TestSystemHttp">
            <text class="button-text">测试系统网络</text>
          </div>
        </div>
        <text v-if="networkTestResult" class="network-result">{{ networkTestResult }}</text>
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
      toast: '',
      networkTesting: false,
      networkTestResult: '',
      pollToken: null,
      polling: false
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
      this.status = '密钥已保留，可以修改后重新继续。'
      this.hint = '请使用手机哔哩哔哩扫描二维码'
    },
    TestSystemHttp() {
      if (this.networkTesting) return
      const http = typeof $falcon !== 'undefined' &&
        $falcon.jsapi && $falcon.jsapi.http
      if (!http || typeof http.request !== 'function') {
        this.networkTestResult = '系统 HTTP JSAPI 不可用'
        return
      }

      this.networkTesting = true
      this.networkTestResult = '正在测试系统 HTTPS…'
      const finish = (message) => {
        this.networkTesting = false
        this.networkTestResult = message
      }

      try {
        http.request({
          url: 'https://passport.bilibili.com/x/passport-login/web/qrcode/generate',
          method: 'GET',
          header: {
            'User-Agent': 'Mozilla/5.0'
          }
        }, (response) => {
          if (!response || typeof response !== 'object') {
            finish('请求已回调，但响应格式无法识别')
            return
          }

          const headers = response.headers || response.header || null
          const headerKeys = headers && typeof headers === 'object'
            ? Object.keys(headers)
            : []
          const cookieKey = headerKeys.find(
            key => key.toLowerCase() === 'set-cookie'
          )
          const cookieValue = cookieKey ? headers[cookieKey] : null
          const cookieCount = Array.isArray(cookieValue)
            ? cookieValue.length
            : (cookieValue ? 1 : 0)
          const status = response.status || response.statusCode ||
            response.status_code || response.httpStatus ||
            response.responseCode || '未知'
          const body = response.result || response.body ||
            response.text || response.data || response.response || ''
          let apiCode = '未解析'
          let bodyKeys = []
          if (typeof body === 'string') {
            try {
              const parsed = JSON.parse(body)
              if (parsed && typeof parsed === 'object') {
                if (typeof parsed.code === 'number') apiCode = String(parsed.code)
                bodyKeys = Object.keys(parsed).slice(0, 8)
              }
            } catch (error) {}
          } else if (body && typeof body === 'object') {
            if (typeof body.code === 'number') apiCode = String(body.code)
            bodyKeys = Object.keys(body).slice(0, 8)
          }

          const responseKeys = Object.keys(response).slice(0, 12)
          finish(
            'HTTPS 返回：' + status +
            ' · B站 API：' + apiCode +
            ' · 响应字段：' + (responseKeys.join(',') || '无') +
            ' · 响应头：' + headerKeys.length + ' 项' +
            ' · Set-Cookie：' + cookieCount + ' 项' +
            (bodyKeys.length ? ' · Body字段：' + bodyKeys.join(',') : '')
          )
        })
      } catch (error) {
        finish('系统 HTTP 请求失败：' +
          (error && error.message ? error.message : String(error)))
      }
    },
    async UnlockVault() {
      if (this.vaultPassword.length < 8) {
        this.showToast('数字密钥至少需要 8 位')
        return
      }
      this.status = '正在验证凭证密钥…'
      try {
        const result = await biliclient.login(false, this.vaultPassword)
        if (result && result.success) {
          this.vaultPassword = ''
          $falcon.navTo('dmlist')
          return
        }
        this.status = '尚无可用登录凭证，请在下一页扫码登录。'
        await this.GetQR()
      } catch (error) {
        this.status = '密钥验证失败，请检查密钥是否正确。'
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
        if (result.success) {
          this.StopPolling()
          this.vaultPassword = ''
          $falcon.navTo('dmlist')
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

.network-test-button {
  height: 34px;
  min-width: 112px;
  margin-top: 9px;
  margin-left: 8px;
}

.network-result {
  margin-top: 5px;
  font-size: 11px;
  color: #b9e6ff;
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

.qr-info {
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
