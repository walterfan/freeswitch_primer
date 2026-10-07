# 第 14 天：WebRTC 到 SIP 的桥接与 codec 路径

## 今日成果

- 完成浏览器 1000 呼叫 SIP 软电话 1001（或反向），并分别记录信令与双向听感。
- 能指出两腿可能使用不同 codec，由 FreeSWITCH 转码。
- 能用 `show channels` 同时看到 `sofia/internal` 的 WebRTC 腿与 SIP/RTP 腿。

## 核心原理

目标链路：SIP.js --WSS--> Sofia internal --session--> bridge --> Sofia --UDP SIP/RTP--> 软电话。Crow 网站不在媒体路径上。A-leg 可能是 OPUS+DTLS-SRTP，B-leg 可能是 PCMU+明文 RTP。core 在中间转码；转码失败时常见“一侧有声”。

`Local_Extension` 仍按 `${destination_number}` 匹配 1001，与运输是 WSS 还是 UDP 无关。

## 源码导航

- [`conf/vanilla/dialplan/default.xml`](../../../../conf/vanilla/dialplan/default.xml)：`bridge user/...`
- [`src/switch_ivr_originate.c`](../../../../src/switch_ivr_originate.c)
- [`tutorial/tests/browser-to-sip-acceptance.md`](../../../tests/browser-to-sip-acceptance.md)
- [`tutorial/site/web/sip_adapter.mjs`](../../../site/web/sip_adapter.mjs)
- [本课实验目录](../../../labs/day-14/)

## 源码深挖

桥接时两条 channel 各自拥有 endpoint 和 media handle。A-leg 可能由 `mod_sofia` 的 WSS transport 创建，B-leg 可能由同一 endpoint 的 UDP transport 创建；`switch_ivr_originate` 创建 B-leg，bridge 层再把双方的读写方向连接起来。编解码是否一致由两腿 media negotiation 决定，不由 dialplan 的 `bridge` 字符串决定。

如果 A-leg 是 Opus/DTLS-SRTP、B-leg 是 PCMU/RTP，FreeSWITCH 会在 core media 中对两侧做采样/编码转换；所以要分别读两个 UUID 的 `read_codec`、`write_codec`，不要只看一侧的 SDP。单向声音时还要区分“转码失败”和“某一腿没有 RTP”。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_ivr_originate|switch_ivr_bridge|switch_core_media_sdp_map|read_codec|write_codec" \
  "$FREESWITCH_SRC/src/switch_ivr_originate.c" \
  "$FREESWITCH_SRC/src/switch_ivr_bridge.c" \
  "$FREESWITCH_SRC/src/switch_core_media.c"
```

桥接验收至少需要四份证据：两条 channel、两条 SDP/codec 结果、两方向 RTP 包、两方向听感。SIP `200 OK` 只覆盖信令层。

## 引导实验

前置条件：浏览器 1000 可 echo 9196；软电话 1001 已 UDP 注册。

1. 浏览器拨 `1001`，软电话接听。`show channels` 应有两行。笔记：A-leg 运输（WSS）、B-leg 运输（UDP）、短 UUID。

2. 双方各说一句。按验收清单单独勾选：1000→1001 能听、1001→1000 能听。任一方向失败则本课媒体为 fail，即使 SIP 状态是 established。

3. 对两腿分别 `uuid_dump`，只比较 `read_codec`/`write_codec`。若不同，在笔记写“发生转码”；不要据此改 vanilla profile。

4. 反向：软电话 1001 拨 1000，浏览器接听。再记录一次双向听感。挂断后 `show channels` 为空。

5. 对照 [`browser-to-sip-acceptance.md`](../../../tests/browser-to-sip-acceptance.md) 的信令勾选项：registered / ringing / established / terminated。自动勾选不能代替听感。

清理：注销两端。删除 uuid_dump。

## 独立挑战

假设只有浏览器→软电话单向有声。写出你检查的顺序：浏览器麦克风 → 浏览器 SRTP 发送 → FS 转码 → SIP RTP 发送 → 软电话听筒。每步一条证据。

## 验收

**验收方式：半自动加人工。**

- **pass**：两腿可见；信令完整；双向听感有记录。
- **fail**：established 但明确单向或无声。
- **unavailable**：缺少第二部 SIP UA。

## 故障排查

- 现象：软电话振铃浏览器无声 → 证据：B-leg RINGING、A-leg 无 RTP → 原因：ICE 未完成就开始 bridge 听感期望 → 恢复：先 9196。
- 现象：转码 CPU 高或破裂音 → 证据：opus↔PCMU → 原因：时钟/丢包 → 恢复：对齐 codec 减少转码（实验允许，不作为本课必做）。
- 现象：网页状态与 fs_cli 不一致 → 证据：页面 established、channels 已空 → 原因：UA 未处理 BYE → 恢复：以 channels 为准。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 桥接路径 | bridge path | WebRTC 腿与 SIP 腿在 core 相遇 |
| 转码 | transcoding | 两腿 codec 不同时的媒体转换 |
| 运输 | transport | WSS 与 UDP SIP 是不同承载 |
