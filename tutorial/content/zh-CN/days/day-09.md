# 第 9 天：SDP、RTP、DTMF 与编解码

## 今日成果

- 从一通内部呼叫归纳 SDP 摘要：音频方向、编解码名称、是否包含 telephone-event，而不保存原始 SDP。
- 用 echo 分机验证 RTP 回环，用 vanilla `5000` IVR 验证 DTMF 到达应用层。
- 能说明“200 OK”只证明信令完成，不证明有声音或 DTMF 可用。

## 核心原理

SDP 在 INVITE/200 中协商媒体。RTP 传送连续音频。DTMF 在本 lab 的 internal profile 使用 RFC 2833（telephone-event），不是把按键混进 PCMU 波形。Sofia 与 UA 的 codec 列表必须有交集；internal 已启用 OPUS，软电话若只开一个互斥 codec 会导致转码或失败。

vanilla 实用分机（均在 `default` context）：

| 号码 | extension | 用途 |
|---|---|---|
| 9196 | echo | 立刻回声，验证本端麦克风和 RTP |
| 9195 | delay_echo | 5 秒延迟回声 |
| 9197 | milliwatt | 1004 Hz 测试音 |
| 5000 | ivr_demo | `demo_ivr`，需要声音包；缺文件则为 unavailable |

## 源码导航

- [`src/switch_rtp.c`](../../../../src/switch_rtp.c)：RTP 会话。
- [`src/mod/endpoints/mod_sofia/sofia_glue.c`](../../../../src/mod/endpoints/mod_sofia/sofia_glue.c)：SDP/codec 粘合。
- [`conf/vanilla/sip_profiles/internal.xml`](../../../../conf/vanilla/sip_profiles/internal.xml)：codec 与 DTMF 相关 param。
- [`conf/vanilla/dialplan/default.xml`](../../../../conf/vanilla/dialplan/default.xml)：`echo` / `ivr_demo`。
- [本课实验目录](../../../labs/day-09/)

## 源码深挖

Sofia 负责把 SIP/SDP 的 offer/answer 交给 media core；`switch_core_media_sdp_map` 把远端 payload type、codec 名称、采样率和 feedback 能力映射到引擎。RTP socket 由 `switch_rtp_create`/`switch_rtp_new` 创建，收包走 `switch_rtp_read`，发帧走 `switch_rtp_write_frame`。因此 SDP 中的 codec 交集成立，仍不代表 UDP 已经能到达。

DTMF 也有两条层次：RFC2833/telephone-event 作为 RTP payload 到达后，由 `switch_rtp` 排队；上层再通过 `switch_rtp_has_dtmf`/`switch_rtp_dequeue_dtmf` 取出，`read` 应用才会把它当作按键。SIP INFO 或 in-band 音调不能直接假定等价。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_core_media_sdp_map|switch_rtp_create|switch_rtp_new|switch_rtp_read|switch_rtp_write_frame|switch_rtp_dequeue_dtmf|telephone-event" \
  "$FREESWITCH_SRC/src/switch_core_media.c" "$FREESWITCH_SRC/src/switch_rtp.c"
```

抓包先看 SDP 的 `m=` 行和 payload map，再看 RTP 的五元组、SSRC、序号/时间戳，最后看 DTMF event payload。不要看到“200 OK”就跳过媒体证据。

## 引导实验

前置条件：1000 已注册。先做 echo，再考虑 IVR。

1. 1000 拨 `9196`。应听到自己的回声。这是媒体平面的最小证明。

2. 通话中取只读媒体摘要（把 UUID 换成 `show channels` 里的完整值，笔记只留前 8 位）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'uuid_dump <UUID>'
   ```

   从 dump 中只抄：`read_codec`、`write_codec`、`dtmf_type`。不要保存 SDP 原文。挂断。

3. 核对 profile 声明的能力：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia status profile internal' \
     | grep -E 'CODECS|DTMF'
   ```

   本 lab 已验证 codec 含 OPUS，DTMF 为 RFC2833。软电话应至少开启 PCMU 或 OPUS 之一，以及 telephone-event。

4. 若容器内存在演示声音包，拨 `5000` 并按 `1`。听不到提示音时记 **unavailable**（缺 sounds），不要标 fail。DTMF 是否被 IVR 消费，以是否进入下一菜单为准。

5. 对比实验：在 UA 里临时只启用一个 FreeSWITCH **未**提供的 codec，再拨 9196。预期失败或无声。恢复 codec 列表。不要改服务器 profile 来迁就错误的 UA。

清理：恢复 UA codec。删除 uuid_dump 全文。

## 独立挑战

列出证明“有声音”所需的三层证据：SDP 有共同 codec、RTP/echo 能听到、应用层收到 DTMF。说明为何 200 OK 只覆盖第一层中的信令部分。

## 验收

**验收方式：半自动加人工。**

- **pass**：9196 回声成功；能写出本通呼叫的 read/write codec；DTMF 实验有结果或明确 unavailable。
- **fail**：信令接通但 echo 无声，且 codec 摘要显示无交集。
- **unavailable**：无麦克风或无声音包。

## 故障排查

- 现象：无共同 codec → 证据：uuid_dump 的 codec 为空或异常 → 原因：UA 与 profile 列表无交集 → 恢复：UA 打开 PCMU/OPUS。
- 现象：有回声但 IVR 不认按键 → 证据：echo 成功、5000 无动作 → 原因：DTMF 模式不是 RFC2833，或 IVR 声音缺失 → 恢复：对齐 dtmf_type；缺文件标 unavailable。
- 现象：tcp 5060 通但仍无声 → 证据：信令在、RTP 端口不通 → 原因：防火墙/NAT → 恢复：第 10 天。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 会话描述 | SDP | 协商媒体地址、方向与 payload |
| 实时传输 | RTP | 连续音频采样的传输 |
| 电话事件 | telephone-event | RTP 中携带 DTMF 的负载 |
| 回声测试 | echo test | 把收到的 RTP 原样送回本端 |
| 转码 | transcoding | 两端 codec 不同时由 core 转换 |
