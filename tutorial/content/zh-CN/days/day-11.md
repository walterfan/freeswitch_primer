# 第 11 天：浏览器安全上下文、麦克风与设备

## 今日成果

- 用教学页的 preflight 区分：非安全上下文、无 mediaDevices、权限拒绝、没有输入设备。
- 确认 SIP 密码只存在于当前页面内存，刷新后不会从 localStorage 恢复。
- 明白麦克风问题属于浏览器安全/设备平面，不是 Sofia 注册失败。

## 核心原理

`getUserMedia` 要求安全上下文：`https://` 或 `http://localhost` / `127.0.0.1`。用 `http://10.100.212.8` 打开网站会直接没有麦克风。设备标签往往要在授权后才完整。输入约束影响本地 MediaStreamTrack；输出通过 `setSinkId`（若浏览器支持）影响听筒。

教学页从 `/api/v1/public-config` 读取 `wss_url` 和 `domain`，**不会**下发 ESL 或 SIP 密码。密码由你在表单输入，保存在 JS 内存。

## 源码导航

- [`tutorial/site/web/preflight.mjs`](../../../site/web/preflight.mjs)：组件与浏览器检查顺序。
- [`tutorial/site/web/media_devices.mjs`](../../../site/web/media_devices.mjs)：设备枚举。
- [`tutorial/site/src/http_server.cpp`](../../../site/src/http_server.cpp)：`/api/v1/public-config`。
- [`tutorial/site/config/localhost-https.yaml`](../../../site/config/localhost-https.yaml)：本机 HTTPS 示例。
- [本课实验目录](../../../labs/day-11/)

## 源码深挖

本课故意从 FreeSWITCH 之外开始：`getUserMedia`、设备枚举和 secure context 都由浏览器决定，Sofia 还没有机会参与。站点的 `/api/v1/public-config` 只返回公开连接参数；SIP 密码在浏览器内存中，避免把认证材料变成可持久化的站点配置。

媒体进入 FreeSWITCH 后才会形成 channel 的 audio engine。`switch_core_media.c` 后续会创建 RTP engine，但如果浏览器根本没有拿到 MediaStream，就不会有有效 SDP/RTP 可供 Sofia 处理。因此“允许麦克风”是媒体协商的前置条件，而不是注册成功后的附加项。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_core_media_sdp_map|switch_rtp_create|SWITCH_ADD_API|public-config|mediaDevices|getUserMedia" \
  "$FREESWITCH_SRC/src/switch_core_media.c" "$FREESWITCH_SRC/src/switch_rtp.c" \
  tutorial/site/src tutorial/site/web
```

先在浏览器控制台确认 `isSecureContext`、`navigator.mediaDevices`、权限和 track 数量，再看 REGISTER；顺序反过来会把浏览器权限问题误判成 SIP 故障。

## 引导实验

前置条件：能打开教学网站。本课**不要求** SIP 注册成功。

1. 用安全上下文打开页面：

   ```bash
   curl -fsS http://127.0.0.1:7009/api/v1/health
   curl -fsS http://127.0.0.1:7009/api/v1/public-config
   ```

   确认 JSON 里没有 password 字段。浏览器打开 `http://127.0.0.1:7009/#day-11`。开发者工具 Console 执行：

   ```javascript
   window.isSecureContext
   navigator.mediaDevices ? "mediaDevices-ok" : "missing"
   ```

   预期 `true` 与 `mediaDevices-ok`。

2. 点击站点的麦克风 preflight：允许一次、拒绝一次、拔掉或禁用输入设备一次。三种状态必须能区分。拒绝后不要把系统对话框截图发到公开渠道。

3. 选择输入/输出设备后刷新。课程进度可以还在；SIP 密码框必须是空的。在 Application → Local Storage 确认没有口令键。

4. 反例：若误用 `http://<局域网IP>:7009` 打开，记录 `isSecureContext===false`，然后改回 localhost 或第 12 天的 HTTPS。不要在浏览器里勾选“允许不安全内容”来骗过 getUserMedia。

清理：撤销误授权的麦克风权限（浏览器站点设置）。不要保存含密码的 HAR。

## 独立挑战

给新同学写 preflight 顺序，必须先 `isSecureContext`，再 `mediaDevices`，再权限，最后才谈 WSS 证书。解释为什么这一天即使 Sofia 挂了，正文仍应可读。

## 验收

**验收方式：自动加人工。**

- **pass**：`public-config` 无密码；安全上下文为 true；三种麦克风结果可复述。
- **fail**：密码出现在 URL、localStorage 或 public-config。
- **unavailable**：无浏览器或无音频硬件。

## 故障排查

- 现象：提示不安全 → 证据：`isSecureContext` false → 原因：非 localhost HTTP → 恢复：loopback 或可信 HTTPS。
- 现象：权限已允许仍无输入 → 证据：设备列表为空 → 原因：系统隐私、USB 未授权、独占设备 → 恢复：OS 设置，不是 fs_cli。
- 现象：有输入无输出 → 证据：远端 track 有、听不到 → 原因：sink 设备或 `<audio>` 未连接 → 恢复：检查 `media_devices.mjs` 与系统输出。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 安全上下文 | secure context | 浏览器允许敏感 API 的页面环境 |
| 媒体设备 | media device | 麦克风或扬声器 |
| 预检 | preflight | 呼叫前对依赖的声明式检查 |
| 公开配置 | public-config | 可下发到浏览器的非密钥 SIP 参数 |
