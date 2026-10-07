# 第 15 天：WSS、注册、ICE、codec 与无声故障挑战

## 今日成果

- 用一张分层表把“浏览器打不通/无声”停在最早失败层。
- 完成一次故意故障演练（错误 SAN 或拒绝麦克风），并恢复。
- 对照第 11–14 天，不再用同一条 `reloadxml` 解决所有问题。

## 核心原理

故障必须分层，禁止跳层：

1. 教学页可达、secure context、麦克风权限  
2. `public-config` 的 `wss_url` 为 wss，证书链+SAN  
3. Sofia WSS 监听、REGISTER Digest  
4. ICE/DTLS  
5. codec 交集与转码  
6. dialplan/bridge 与对端注册  
7. 人工听感  

每一层有自己的证据。健康 JSON 里 `sip_profile` healthy 不能推出第 7 层通过。

## 源码导航

- [`tutorial/site/web/preflight.mjs`](../../../site/web/preflight.mjs)
- [`tutorial/site/web/diagnostics.mjs`](../../../site/web/diagnostics.mjs)
- [`tutorial/tests/browser-to-sip-acceptance.md`](../../../tests/browser-to-sip-acceptance.md)
- 第 11–14 天正文
- [本课实验目录](../../../labs/day-15/)

## 源码深挖

本课的核心是把故障停在最早失败点。WSS 连接失败看 Sofia transport/TLS；REGISTER 失败看 `sofia_reg_handle_register_token`；SDP/ICE/DTLS 失败看 `switch_core_media_sdp_map`、ICE 标志和 DTLS state；无声才进入 RTP、codec 和 bridge。每层的修复动作不同，`reloadxml` 不能修复浏览器权限或网络丢包。

推荐用以下命令把源码入口与日志关键词并排查找：

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "sofia_event_callback|sofia_reg_handle_register_token|switch_core_media_sdp_map|switch_rtp_read|switch_rtp_write_frame|CF_DTLS_OK" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia" \
  "$FREESWITCH_SRC/src/switch_core_media.c" "$FREESWITCH_SRC/src/switch_rtp.c"
```

故意故障时只改变一个变量，例如证书 SAN 或麦克风权限；恢复后重新跑完整链路。否则多个层同时变化，实验无法说明哪一个证据证明了修复。

## 引导实验

前置条件：第 14 天至少成功过一次浏览器 echo。

1. 复制并填写分层表（每层 pass/fail/unavailable + 一条证据）：

   | 层 | 结果 | 证据 |
   |---|---|---|
   | 页面/安全上下文 |  | `isSecureContext` |
   | 麦克风 |  | 权限状态 |
   | WSS/证书 |  | s_client 或浏览器锁图标 |
   | REGISTER |  | `internal reg` 有 1000 |
   | ICE/DTLS |  | webrtc-internals 状态名 |
   | codec |  | uuid_dump 的 codec 名 |
   | bridge |  | show channels 两腿 |
   | 听感 |  | 人工 |

2. **演练 A**：拒绝麦克风后尝试呼叫。预期停在第 1 层。允许权限后 9196 恢复。

3. **演练 B**（可选，有证书回滚条件时）：把浏览器指向错误主机名的 wss URL。预期停在第 2 层。恢复 `localhost-https.yaml` 中的正确 `wss_url`。

4. 用验收清单走一遍 1000→1001，但把听感栏留空直到真正听过。禁止根据 established 自动打勾。

清理：恢复证书与权限。siptrace 关闭。

## 独立挑战

写给值班同事一页纸：8 层各对应“不要做的事”（例如第 2 层失败时不要 unload mod_sofia）。

## 验收

**验收方式：手动。**

- **pass**：分层表完整；至少一次故障演练有恢复步骤；听感栏未被信令冒充。
- **fail**：用 `hupall` 当唯一修复手段。
- **unavailable**：无法访问浏览器。

## 故障排查

- 现象：所有层看起来 pass 仍无声 → 证据：表已填满 → 原因：输出设备或远端静音 → 恢复：第 11 天 sink、第 10 天 RTP。
- 现象：多层同时红 → 证据：证书与麦克风一起失败 → 原因：一次改了太多变量 → 恢复：回滚到只改一层。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 分层排障 | layered diagnosis | 按依赖顺序停止在最早失败 |
| 故障演练 | failure drill | 故意破坏再恢复以验证 runbook |
| 听感验收 | listening acceptance | 人耳确认，不能由信令自动完成 |
