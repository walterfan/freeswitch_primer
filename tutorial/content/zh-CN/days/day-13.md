# 第 13 天：WebRTC SDP、ICE、DTLS-SRTP 与 Opus

## 今日成果

- 解释浏览器媒体路径：SDP → ICE 候选 → DTLS 握手 → SRTP，与 SIP 软电话的明文 RTP 不同。
- 从 Sofia/浏览器侧只保存 codec 与 ICE 状态摘要，不保存完整 SDP。
- 确认 internal profile 提供 OPUS，且教学页只申请音频、不申请视频。

## 核心原理

WebRTC 音频在浏览器里是 `RTCPeerConnection`。SIP.js 把 SDP 放进 SIP INVITE。ICE 为每个媒体流找可用的 IP:port；失败时信令可能已 183/200，但 DTLS 未完成，表现为“已接通无声音”。DTLS-SRTP 保护浏览器与 FreeSWITCH 之间的媒体。Opus 是 WebRTC 常用音频 codec；FS 侧需要 profile 包含 OPUS，否则要转码到 PCMU，转码失败则无声。

教学适配器 `media.constraints` 固定 `video: false`。

## 源码导航

- [`tutorial/site/web/sip_adapter.mjs`](../../../site/web/sip_adapter.mjs)：音频约束与 wss 强制。
- [`conf/vanilla/sip_profiles/internal.xml`](../../../../conf/vanilla/sip_profiles/internal.xml)：codec / ws / wss。
- [`src/mod/endpoints/mod_sofia/mod_sofia.c`](../../../../src/mod/endpoints/mod_sofia/mod_sofia.c)
- [`src/switch_rtp.c`](../../../../src/switch_rtp.c)：SRTP/DTLS 相关路径。
- [本课实验目录](../../../labs/day-13/)

## 源码深挖

远端 SDP 进入 `switch_core_media_sdp_map` 后，FreeSWITCH 为 audio/video/text 分别建立 media engine。检测到 ICE 时设置 channel 的 ICE 标志；检测到 DTLS 参数时生成/读取 fingerprint，并在 RTP session 上调用 DTLS 初始化。SDP 的 `a=ice-ufrag`、`a=ice-pwd`、candidate、fingerprint 和 `setup` 是不同字段，缺一项都可能导致后续层失败。

对 WebRTC 音频，Opus 的 payload map 还会经过特殊参数解析；如果两腿 codec 不交集，FreeSWITCH 才可能走转码，转码路径本身还需要对应 codec 模块。不要把“SDP 中出现 opus”解释为“RTP 已经收到 opus”。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_core_media_sdp_map|switch_determine_ice_type|CF_ICE|CF_DTLS|switch_rtp_add_dtls|opus" \
  "$FREESWITCH_SRC/src/switch_core_media.c"
```

排障记录按顺序写：offer/answer 的 m-line → ICE candidate pair → DTLS fingerprint/state → RTP/RTCP 包 → codec 读写。任意一步没有证据，就不要跳到下一层修改配置。

## 引导实验

前置条件：第 12 天 WSS 验证码 0；第 11 天麦克风允许。

1. 浏览器注册 1000。`sofia status profile internal reg` 应出现该用户（笔记只写“浏览器 1000 已注册”）。

2. 从教学页呼叫 `9196`（echo）。人工记录：能否听到自己的回声。这是 WebRTC 媒体最小闭环，不经过第二个 SIP 话机。

3. 通话中在 Docker 主机：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels'
   ```

   `uuid_dump` 只抄 `read_codec`/`write_codec` 是否含 opus 或转码后的 PCMU。Chrome 可在 `chrome://webrtc-internals` 看 ICE `state: completed` 与 `dtlsState: connected`；不要导出完整 stats JSON 到 git。

4. 若回声失败，按层打勾：WSS 是否仍连接、REGISTER 是否仍 200、ICE 是否 failed、DTLS 是否 connected、codec 是否 opus。停在最早失败层。

清理：挂断并注销。不要提交 webrtc-internals 导出。

## 独立挑战

画四段管道：SIP/WSS 信令、ICE 连通性、DTLS 密钥、Opus/RTP。给每一段一个“失败时听感”和一条允许的证据命令或浏览器面板。

## 验收

**验收方式：半自动加人工。**

- **pass**：浏览器能注册；9196 回声有人工结论；codec 摘要不含完整 SDP。
- **fail**：ICE failed 却去改 dialplan。
- **unavailable**：浏览器不支持 WebRTC 或无麦克风。

## 故障排查

- 现象：registered 但 echo 无声 → 证据：ICE/DTLS 未 completed → 原因：UDP 候选被墙、证书与媒体地址不一致 → 恢复：检查防火墙与第 10/12 天，不关 DTLS。
- 现象：只有 opus 对端只有 PCMU 且无转码模块 → 证据：codec 无交集 → 原因：profile/UA 列表 → 恢复：对齐 codec。
- 现象：视频灯在闪 → 证据：约束含 video → 原因：改了适配器或浏览器扩展 → 恢复：保持 audio-only。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 交互式连接建立 | ICE | 为 WebRTC 媒体选择可达候选 |
| 数据报传输层安全 | DTLS | 为 SRTP 提供密钥协商 |
| 安全实时传输 | SRTP | 加密后的 RTP |
| 操作码音频 | Opus | WebRTC 常用音频编码 |
