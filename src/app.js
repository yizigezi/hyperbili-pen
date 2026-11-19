import { BasePage } from './base-page.js'

class App extends $falcon.App {
  constructor() {
    super()
  }

  onLaunch(options) {
    super.onLaunch(options)
    $falcon.useDefaultBasePageClass(BasePage)
  }

  onShow() {
    super.onShow()
  }

  onHide() {
    super.onHide()
  }

  onDestroy() {
    super.onDestroy()
  }
}

// 与 doge-reader / readex-nt 保持一致：为 Falcon/QuickJS 页面运行环境补齐基础全局对象。
try {
  globalThis['window'] = {
    requestAnimationFrame,
    cancelAnimationFrame
  }
} catch (err) {
  console.log(err)
}

try {
  globalThis['process'] = {
    env: {
      NODE_ENV: 'production'
    }
  }
} catch (err) {
  console.log(err)
}

export default App
