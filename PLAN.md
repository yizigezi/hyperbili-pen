# hyperbili-pen 移植计划

把 HyperBilibili（Vela 版澎湃哔哩，Searchstars/HyperBilibili）的**登录 + 私信**搬到有道词典笔（Falcon miniapp）。
纯前端实现，性能问题先搁置，之后再评估原生模块。框架沿 readex-nt 惯例（aiot-vue-cli + Falcon → .amr）。

## 一、调研结论（已核实，不是猜的）

| 能力 | 词典笔上的实现 | 依据 |
|---|---|---|
| 网络请求 | `$falcon.jsapi.http.request({url,method,header}, cb)` | weatheronyddictpen 实证（纯前端可直接打 B 站 API） |
| 本地存储 | `import('storage')`（get/set/delete） | doge-comic / readex 实证 |
| 二维码渲染 | falcon-ui 自带 `qr-code` 组件 | falcon-ui@2.0.2 packages 清单 |
| shell 兜底 | `global.execShell` | PenMusic 文档（readex-nt FileSystem.js 同款思路） |
| 构建链 | aiot-cli → .amr → `miniapp_cli install` | readex-nt install.sh |

HyperBilibili 侧可移植的资产：
- **API 层**（bilibiliclient）：QR 登录轮询、Wbi 签名、私信 session 列表 / 聊天记录 / 发送——逻辑照搬，接口适配
- **UI 素材**：287 张 png（头像占位、气泡、图标）挑着用；粉色主题 `#ff7da8`、圆角白卡片风格
- **页面参考**：login.ux（93 行）、dmlist.ux（135 行）、dmpage.ux（222 行）——体量不大

## 二、两个关键风险（P1 先啃）

1. **QR 登录要从响应头抠 Set-Cookie**（SESSDATA / bili_jct / DedeUserID）。
   `$falcon.jsapi.http` 回调是否带 response headers **未证实**。
   备选链：headers 可用 → 直接干 ＞ `execShell('curl -i ...')` 抠头 ＞ 开发期手动粘贴 SESSDATA（链路验证兜底）。
2. **发消息要文字输入**，falcon-ui 没有现成 input 组件，设备 `<input>` 支持未证实。
   备选链：`<input>` 可用 → 直接干 ＞ 自绘屏幕键盘（HyperBilibili 的 FullScreenInput 思路）＞ 快捷短语先验证发送链路。

## 三、架构

```
hyperbili-pen/
├── package.json          # readex-nt 同款构建脚本
├── install.sh            # build:prod + adb 推送
├── api-mock/             # web 预览用 http/storage mock
├── src/
│   ├── app.js / app.json / base-page.js
│   ├── bilibiliclient/   # API 层（从 HyperBilibili 移植改写，GPL-3.0）
│   │   ├── request.js    # 请求头/Cookie/Wbi 签名/GET/POST
│   │   ├── login.js      # QR 生成轮询、Cookie 提取、账号存取
│   │   └── message.js    # 会话列表/聊天记录/发送私信
│   ├── pages/
│   │   ├── login/        # 扫码登录
│   │   ├── dmlist/       # 私信会话列表
│   │   └── dmpage/       # 聊天页（气泡 + 发送）
│   ├── components/       # 消息气泡、发送框、全屏输入
│   ├── utils/            # http 适配、storage 适配、md5（纯 JS）、logger
│   ├── styles/           # HyperBilibili 色板与卡片样式
│   └── assets/           # 复用素材
└── docs/                 # 真机探针记录、API 笔记
```

## 四、分期

- **P0 骨架**：工程模板、http/storage 适配层（带 mock）、build:web 跑通
- **P1 登录**：QR 生成 + 轮询 + Cookie 落储 + 登录态校验（nav 接口）；**先真机探针啃风险 1**
- **P2 私信**：会话列表 → 聊天记录 → 发送（风险 2 的输入方案定型后补键盘）
- **P3 打磨**：HyperBilibili 视觉还原（头像/气泡/间距/loading）、错误处理、登出
- **之后**：性能评估（长会话列表滚动、图片加载）→ 再决定原生模块范围

## 五、验证

- 迭代期：`npm run build:web` + api-mock 看 UI
- 真机：`./install.sh` 推 .amr，登录用主人手机扫码实测
- 登录/私信各阶段都要真机过一遍才算数（跑腿验证，不空口交差）

## 六、许可

- HyperBilibili：**GPL-3.0-only** → 移植代码与素材，本项目同样以 GPL-3.0 发布（与 readex-nt 一致）
- doge-reader / doge-comic：AGPL-3.0 → **只参考 API 形状与做法，不抄代码**（沿 readex-nt 的红线）

## 七、进度记录

### 2026-11-18 P0 骨架 + P1/P2 核心代码完成，web 预览全流程验证通过

- ✅ utils 层：md5.js（10 组向量过，修了 `>>>` 移位 mod-32 坑）、storage.js、http.js 三级降级
- ✅ bilibiliclient：request/login/message/user 四模块 + 单例（QuickApp fetch 信封 {code,data,headers} 形状保持，call site 原样）
- ✅ 页面：login（QR + 轮询 + 业务码 86101/86090/86038）、dmlist、dmpage（消息气泡 + 底部输入发送）
- ✅ api-mock（storage/http/global）：web 预览用假数据，含模拟扫码 4 次轮询后"登录成功"带 Set-Cookie
- ✅ CI workflow `.github/workflows/build.yml`（Node16 + aiot-vue-cli@1.0.32 + check + build:prod + 上传 amr）
- ✅ build:web / build:prod 均通过，产物 `8026111800000001.0_1_0.amr`
- ✅ web 预览 E2E（agent-browser + mimo-omni 看图）：登录页 QR 渲染 → 轮询 → 自动进 dmlist（2 会话 + 用户卡片）→ 进 dmpage（左深右粉气泡）→ 发消息成功清空输入框

### 踩坑记录（重建/移植复用必读）

1. **JS `>>>` 移位量 mod 32**：md5 长度字段写高 32 位要用除法取，不能 `bitLen >>> 56`。
2. **uglify-es 崩溃**（TypeError: reading 'name'，join_object_assignments）：「对象字面量 + 后续 `obj[k]=v` 成员赋值」的组合会炸，改写成一次性展开/三元。
3. **Weex 样式限制**：裸元素选择器（`text {}`）和复合选择器（`.msgbox.mine`）都被丢弃，只认单类选择器；文字颜色要写进各类。
4. **falcon 生命周期桥接**：Page 与 Vue 根组件是两个对象，组件的 onShow/onDestroy 不会自动被调用——base-page.js 手动转发到 `this.$root.X`（组件钩子写在 methods 里）；组件内定时器用 `this.$page.setTimeout/setInterval/clearTimer`。
5. **navTo 参数竞态**：`this.$page.options` 的赋值时机不稳定（可能在 onShow 之后才到），参数要在 onLoad/onNewOptions/onShow 三处都读（applyOptions），必要时 onNewOptions 里重拉数据。
6. **web 预览**：单页 html 不带 falcon 运行时，要开 `_preview.app.html#page=xxx`；dist 缺 `falcon-js-web.js` 时从 `aiot-vue-cli/cli-libs/falcon-jsfm/dist/` 拷入 dist；**改代码后必须硬刷新/新会话**，浏览器缓存旧 bundle 会让你以为没生效。
7. **node_modules 里 css-loader/less-loader** 可能缺失或损坏（less-loader 空目录），从 `.openclaw/tmp/npm-global/node_modules` 软链同版本即可（build:web 需要，build:prod 不需要）。

### 待办（P1 真机）

- [ ] 真机探针：`$falcon.jsapi.http` 是否暴露 response headers（决定 Set-Cookie 能否自动抠；不行就 curl -i 兜底 / 手动贴 SESSDATA）
- [ ] 真机确认 falcon-ui qr-code（canvas）能渲染；不行就退回 div 网格自绘 QR
- [ ] `import('http')`/`import('storage')` 在固件里是否存在（不存在则走 execShell 兜底）
- [ ] dmpage 消息滚到底部（$element/scrollTo 待验证）
- [ ] install.sh 走 adb 实测
