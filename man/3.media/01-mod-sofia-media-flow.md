# 20. mod_sofia：SDP 协商与 RTP setup

<!-- maintained-by: human+ai -->

本文从 FreeSWITCH `mod_sofia` 的实现说明 SDP 协商和 RTP 建立发生在哪里、由什么事件
触发，以及 `sofia_media_negotiate_sdp()` 在不同呼叫阶段为什么使用不同的
`SDP_OFFER` / `SDP_ANSWER` 类型。

## 1. 文件和入口

`sofia_media_negotiate_sdp()` 不在 `src/switch_core_media.c` 中，而定义在：

```text
src/mod/endpoints/mod_sofia/sofia_media.c:37
```

它只是 `mod_sofia` endpoint wrapper，真正的 SDP 解析、codec/payload 匹配和 media
handle 更新由以下 core 函数完成：

```text
sofia_media_negotiate_sdp()
  -> switch_core_media_negotiate_sdp()
     [src/switch_core_media.c:4788]
```

当前源码中，`sofia_media_negotiate_sdp()` 的调用点全部位于
`sofia_handle_sip_i_state()`，而不是直接位于 `switch_core_media.c`。

### 1.1 后台事件到媒体处理

```text
Sofia-SIP su_root_step()                 [Sofia profile thread]
  -> sofia_event_callback()
  -> switch_core_session_queue_signal_data()
  -> sofia_receive_message()             [session thread]
  -> sofia_process_dispatch_event()
  -> our_sofia_event_callback()
  -> case nua_i_state
  -> sofia_handle_sip_i_state()
  -> sofia_media_negotiate_sdp()
```

在 `sofia_handle_sip_i_state()` 中，Sofia-SIP 通过 `tags[]` 传入 NUA call state、远端
SDP 和 offer/answer 标志：

```c
int ss_state = nua_callstate_init;

tl_gets(tags,
        NUTAG_CALLSTATE_REF(ss_state),
        NUTAG_OFFER_RECV_REF(offer_recv),
        NUTAG_ANSWER_RECV_REF(answer_recv),
        NUTAG_OFFER_SENT_REF(offer_sent),
        NUTAG_ANSWER_SENT_REF(answer_sent),
        SOATAG_REMOTE_SDP_STR_REF(r_sdp),
        TAG_END());
```

`ss_state` 是 Sofia NUA 的呼叫状态，不是 FreeSWITCH 的 `CS_*` channel state。两者的
关系见 [Sofia NUA 呼叫状态机](../2.signal/03-sofia-nua-call-model.md) 和
[媒体协商与 RTP activation](../1.architecture/05-workflows.md#media-negotiation-and-rtp-activation)。

## 2. Wrapper 的行为

`sofia_media_negotiate_sdp()` 的核心逻辑如下：

```c
uint8_t t, p = 0;

if ((t = switch_core_media_negotiate_sdp(session, r_sdp, &p, type))) {
    sofia_set_flag_locked(tech_pvt, TFLAG_SDP);
}

if (!p) {
    sofia_set_flag(tech_pvt, TFLAG_NOREPLY);
}

return t;
```

其中：

- 返回值 `t` 表示 SDP/codec 是否匹配成功；
- `type` 表示当前远端 SDP 是 offer 还是 answer；
- 成功匹配后设置 `TFLAG_SDP`；
- core 没有要求立即回复时设置 `TFLAG_NOREPLY`；
- 端口选择和 RTP 激活不是这个 wrapper 自动完成的，调用方根据场景继续调用
  `switch_core_media_choose_port()`、`switch_core_media_gen_local_sdp()` 和
  `sofia_media_activate_rtp()`。

## 3. 全部调用场景

| 源码位置 | NUA 状态 / 条件 | SDP 类型 | 具体场景 |
|---|---|---|---|
| `sofia.c:7812` | `nua_callstate_completing` + `CF_3P_MEDIA_REQUESTED` | `SDP_OFFER` | 3PCC 请求恢复实际媒体，收到远端 offer |
| `sofia.c:7945` | `nua_callstate_received` | `SDP_OFFER` | 入站 INVITE 或初始入站媒体协商 |
| `sofia.c:8153` | `nua_callstate_early` + `answer_recv` | `SDP_ANSWER` | 出站 INVITE 收到 183 等早期媒体 answer |
| `sofia.c:8357` | `nua_callstate_completed` + proxy hold | `SDP_OFFER` | hold/unhold re-INVITE 的 codec 验证 |
| `sofia.c:8456` | `nua_callstate_completed` | `SDP_OFFER` | 普通入站 re-INVITE |
| `sofia.c:8544` | `nua_callstate_ready` + 新 SDP | `SDP_ANSWER` | 已建立呼叫的 SDP answer 更新 |
| `sofia.c:8608` | `nua_callstate_ready` + `TFLAG_GOT_ACK` | `SDP_OFFER` | ACK 后收到的 SDP 被视为新 offer |
| `sofia.c:8610` | `nua_callstate_ready` + 非 `TFLAG_GOT_ACK` | `SDP_ANSWER` | ACK 前收到的 SDP 被视为 answer |
| `sofia.c:8705` | `nua_callstate_ready` + 尚未完成 SDP | `SDP_ANSWER` | 出站呼叫最终收到 answer |

这些调用点都遵循同一条原则：先确认当前 SIP/NUA 状态和 SDP 角色，再把远端 SDP
交给 core 做 codec 匹配；匹配失败通常走 SIP 488 或呼叫失败路径。

## 4. 各阶段的详细流程

### 4.1 入站 INVITE：收到 SDP offer

路径位于 `nua_callstate_received`：

```text
nua_i_state
  -> sofia_handle_sip_i_state()
  -> case nua_callstate_received
  -> sofia_media_negotiate_sdp(session, r_sdp, SDP_OFFER)
  -> switch_core_media_negotiate_sdp()
  -> codec match
  -> switch_channel_set_state(channel, CS_INIT)
```

源码在 `sofia.c:7944-7964`。匹配成功后，新的 channel 从 `CS_NEW` 进入 `CS_INIT`，
随后 core state machine 的 `switch_core_standard_on_init()` 通常将其推进到
`CS_ROUTING`。如果 codec 不匹配，已有 channel 会收到 SIP 488；proxy media、late
negotiation 等模式会走专门分支。

### 4.2 出站 INVITE：收到早期 answer

`nua_callstate_early` 且 `answer_recv` 为真时，远端 SDP 是对本地 offer 的 answer：

```text
nua_callstate_early + answer_recv
  -> sofia_media_negotiate_sdp(..., SDP_ANSWER)
  -> switch_core_media_choose_port()
  -> switch_core_media_gen_local_sdp(..., SDP_ANSWER, ...)
  -> sofia_media_activate_rtp()
  -> early media
```

对应源码为 `sofia.c:8144-8169`。成功后设置 early-media 标志并将 channel 标记为
pre-answered。

### 4.3 出站呼叫：最终收到 answer

如果呼叫还没有完成 SDP，`nua_callstate_ready` 分支在 `sofia.c:8673-8727` 处理远端
answer：

```text
r_sdp && !TFLAG_SDP
  -> sofia_media_negotiate_sdp(..., SDP_ANSWER)
  -> switch_core_media_choose_port()
  -> sofia_media_activate_rtp()
  -> switch_channel_mark_answered()
```

如果匹配失败，endpoint disposition 会记录 codec negotiation error，并返回失败或挂断。

### 4.4 入站 re-INVITE：新的 SDP offer

普通入站 re-INVITE 位于 `nua_callstate_completed`：

```text
re-INVITE with SDP offer
  -> sofia_media_negotiate_sdp(..., SDP_OFFER)
  -> switch_core_media_choose_port()
  -> switch_core_media_gen_local_sdp(..., SDP_ANSWER, ...)
  -> sofia_media_activate_rtp()
  -> 200 OK with SDP answer
```

对应源码为 `sofia.c:8453-8482`。如果 SDP 表示 hold/unhold，代码会先分析
`sendonly`、`inactive` 或 `0.0.0.0`，并可能把媒体变化通知 bridge 的另一条 leg。

`PFLAG_PROXY_HOLD` 分支在 `sofia.c:8353-8359` 也会调用
`SDP_OFFER`，但主要用途是验证 offer 并把 hold 状态传给对端，不一定在当前 leg
立即执行普通 RTP activation。

### 4.5 已建立呼叫中的 SDP 更新

当 `nua_callstate_ready` 检测到新的 SDP，`sofia.c:8534-8558` 使用
`SDP_ANSWER` 重新匹配、生成本地 SDP 并重新激活 RTP。

对于 `TFLAG_NOSDP_REINVITE`，代码根据 ACK 是否已经收到决定 SDP 角色：

```c
if (sofia_test_flag(tech_pvt, TFLAG_GOT_ACK)) {
    match = sofia_media_negotiate_sdp(session, r_sdp, SDP_OFFER);
} else {
    match = sofia_media_negotiate_sdp(session, r_sdp, SDP_ANSWER);
}
```

位置是 `sofia.c:8606-8611`。这处理的是 re-INVITE / ACK 时序下 SDP 角色变化，不能
只根据 SIP 方法名判断 offer 或 answer。

### 4.6 3PCC 媒体恢复

`nua_callstate_completing` 且 `CF_3P_MEDIA_REQUESTED` 时，代码先准备 codec，再把
远端 SDP 当作 offer：

```text
CF_3P_MEDIA_REQUESTED
  -> choose_port()
  -> prepare_codecs()
  -> sofia_media_negotiate_sdp(..., SDP_OFFER)
  -> gen_local_sdp(SDP_ANSWER)
  -> switch_core_media_activate_rtp()
```

对应源码为 `sofia.c:7803-7833`。这是 3PCC 重新建立实际媒体的路径，不是普通初始
INVITE 的 codec 协商。

## 5. 从 SDP 匹配到 RTP 可用

`sofia_media_negotiate_sdp()` 返回成功后，是否立即建立 RTP 取决于调用点。典型 early
media / re-INVITE setup 链为：

```text
sofia_media_negotiate_sdp()
  -> switch_core_media_negotiate_sdp()
  -> switch_core_media_choose_port()
     -> switch_rtp_request_port()
  -> switch_core_media_gen_local_sdp()
  -> sofia_media_activate_rtp()
     -> switch_core_media_activate_rtp()
  -> TFLAG_RTP + TFLAG_IO
  -> switch_rtp_ready() == true
```

`switch_rtp_ready()` 需要同时满足：RTP session 未关闭、输入/输出 socket 存在、远端
地址存在、`SWITCH_RTP_FLAG_IO` 已设置，并且 RTP 内部 `ready == 2`。因此：

```text
SDP match != RTP ready
```

proxy mode / proxy media 可能跳过普通 codec termination；late negotiation 可能先记录
匹配结果，待 dialplan 或 bridge 提供媒体上下文后再完成 local SDP 和 RTP setup。

## 6. `sofia_media_activate_rtp()` 的调用场景

### 6.1 Early media

`sofia_media_tech_media()` 在收到并匹配远端 SDP offer 后直接调用 wrapper：

```text
sofia_media_tech_media()
  -> sofia_media_negotiate_sdp()
  -> switch_core_media_choose_port()
  -> sofia_media_activate_rtp()
  -> mark pre-answered / EARLY MEDIA
```

位置：`sofia_media.c:81-90`。这是普通 early media 的主要入口。

`nua_callstate_early + answer_recv` 是出站呼叫收到 183 等早期 answer 的路径，位置为
`sofia.c:8144-8165`：

```text
SDP_ANSWER
  -> choose_port()
  -> gen_local_sdp(SDP_ANSWER)
  -> sofia_media_activate_rtp()
```

### 6.2 入站呼叫最终 answer

`sofia_answer_channel()` 在入站呼叫最终接听时建立正式 RTP，位置为
`mod_sofia.c:925-933`：

```text
choose_port()
  -> gen_local_sdp(SDP_ANSWER)
  -> sofia_media_activate_rtp()
  -> SIP 200 OK
```

这条路径覆盖 codec 已选定、late negotiation 完成，以及 dialplan/bridge 触发 answer
的情况。

### 6.3 入站 re-INVITE / hold / unhold

普通入站 re-INVITE 在 `nua_callstate_completed` 中匹配 SDP offer 后调用 wrapper，位置为
`sofia.c:8453-8482`：

```text
re-INVITE + SDP_OFFER
  -> codec match
  -> choose_port()
  -> gen_local_sdp(SDP_ANSWER)
  -> sofia_media_activate_rtp()
  -> 200 OK
```

已建立呼叫检测到新的 SDP answer 时，`nua_callstate_ready` 在
`sofia.c:8533-8558` 重新调用 wrapper，用于更新远端地址、payload map 或媒体参数。

### 6.4 NOSDP re-INVITE、ACK 和 3PCC

以下是特殊时序或 3PCC 路径：

| 位置 | 条件 | 作用 |
|---|---|---|
| `sofia.c:1731-1738` | 3PCC proxy 收到 ACK 且携带远端 SDP | 激活 RTP，并更新 proxy remote address |
| `sofia.c:8568-8575` | `TFLAG_NOSDP_REINVITE` + `CF_PROXY_MEDIA` | 已有 SDP 上下文时直接重启/激活 proxy RTP |
| `sofia.c:8606-8619` | `TFLAG_NOSDP_REINVITE` 普通媒体 | 根据 `TFLAG_GOT_ACK` 把 SDP 当 offer 或 answer，匹配后激活 RTP |
| `sofia.c:8638-8648` | `nh == tech_pvt->nh2` 的 cheater re-INVITE | 切换 handle 后重新激活 RTP |
| `sofia.c:8673-8727` | `nua_callstate_ready` 尚未完成 SDP | 收到最终 answer 后激活 RTP，并标记 channel answered |
| `sofia.c:7803-7833` | `CF_3P_MEDIA_REQUESTED` | 3PCC 恢复媒体；此处有一处直接调用 `switch_core_media_activate_rtp()` |

最后一行是一个重要例外：`sofia.c:7832` 没有经过
`sofia_media_activate_rtp()` wrapper，而是直接调用 core 函数。

### 6.5 Proxy media 的直接激活

`CF_PROXY_MEDIA` 不一定经过正常 codec termination。下列路径先设置或 patch SDP，再
直接激活 proxy RTP：

- `sofia.c:7687-7690`：`nua_callstate_proceeding` 的 proxy/early media；
- `sofia.c:7704-7713`：带 SDP 的 proxy media early response；
- `sofia.c:8276-8278`：re-INVITE hold/unhold 后更新 proxy remote address；
- `mod_sofia.c:751`、`:805`、`:834`：3PCC/proxy answer 路径；
- `mod_sofia.c:2580-2611`：发送 progress/183 时的 proxy media。

这些分支的共同形式是：

```text
set_local_sdp() / patch_sdp()
  -> sofia_media_activate_rtp()
  -> switch_core_media_proxy_remote_addr()
```

## 7. RTP 通道搭建流程

下面描述 `sofia_media_activate_rtp()` 进入
`switch_core_media_activate_rtp()` 后，audio RTP channel 的实际建立过程。

### 7.1 前置条件和模式判断

```text
sofia_media_activate_rtp(tech_pvt)
  -> lock tech_pvt->sofia_mutex
  -> switch_core_media_activate_rtp(session)
```

core 首先检查 `session->media_handle` 和 channel 是否仍然 up，并根据已有 RTP session
或 `CF_REINVITE` 判断这是初次建立还是 re-INVITE 更新：

- 没有 RTP session：建立新的 audio RTP engine；
- 已有 RTP session：更新 codec、远端地址、DTLS/ICE 或 timeout 参数；
- `CF_PROXY_MODE`：跳过普通 RTP termination，直接返回成功；
- `CF_PROXY_MEDIA`：使用 proxy RTP flags 和 proxy remote address。

源码：`switch_core_media.c:8551-8587`。

### 7.2 选择 codec 和 RTP flags

core 调用 `switch_core_media_set_codec()` 选择当前 audio codec，并设置 video codec。
然后根据 media flags 和 channel variables 组装 RTP flags，包括：

- `DATAWAIT`：等待媒体数据；
- `AUTOADJ`：RTP/NAT 自动调整；
- RFC2833/telephone-event pass-through；
- `AUTOFLUSH`、raw timestamp、comfort noise；
- little-endian L16 的 byteswap；
- RTCP event、SRTP/DTLS 相关设置。

源码：`switch_core_media.c:8612-8655`。

### 7.3 处理 re-INVITE 的远端地址

如果 RTP session 已存在，core 会比较旧的 remote host/port 和当前 SDP 中的地址：

```text
existing rtp_session
  -> compare old remote address
  -> switch_rtp_set_remote_address()
  -> check_dtls_reinvite()
```

源码：`switch_core_media.c:8657-8732`。地址改变时会更新 channel 变量并触发
`execute_on_audio_change`。

### 7.4 创建 RTP socket/session

初始建立时，`switch_core_media_choose_port()` 已经通过
`switch_rtp_request_port()` 选好了本地端口；`switch_core_media_activate_rtp()` 随后
使用本地 IP/端口、远端 SDP IP/端口、payload type、采样间隔和 flags 创建 RTP session：

```text
switch_rtp_new(
    local_sdp_ip,
    local_sdp_port,
    remote_sdp_ip,
    remote_sdp_port,
    payload_type,
    samples_per_packet,
    codec_ms,
    rtp_flags,
    timer_name
)
```

audio 创建位置：`switch_core_media.c:8770-8786`。创建成功后设置 payload map、读写
mutex、SSRC、`CF_FS_RTP` 以及当前 payload type。

### 7.5 ICE、RTCP、DTLS 和加密

RTP session 建立后，core 按 SDP 和 profile 配置继续挂接辅助通道：

```text
RTP session
  -> ICE activation, if ICE candidates are ready
  -> RTCP port or RTCP-mux activation
  -> DTLS-SRTP setup, if fingerprints are present
  -> switch_core_session_apply_crypto()
```

相关源码：

- ICE：`switch_core_media.c:8860-8879`；
- RTCP：`switch_core_media.c:8885-8935`；
- DTLS：`switch_core_media.c:8938-8960`；
- SRTP/crypto 应用：`switch_core_media.c:9036`。

### 7.6 Ready gate 和 RTP 收发

`switch_rtp_ready()` 只有在下列条件全部满足时返回 true：

```text
RTP session exists
  && not shutdown
  && input socket exists
  && output socket exists
  && remote_addr exists
  && SWITCH_RTP_FLAG_IO is set
  && ready == 2
```

当 wrapper 成功返回时，`sofia_media_activate_rtp()` 设置 `TFLAG_RTP` 和 `TFLAG_IO`；
随后 endpoint IO 才能真正收发 RTP：

```text
switch_core_session_read_frame()
  -> sofia_read_frame()
  -> switch_core_media_read_frame()
  -> switch_rtp_zerocopy_read_frame()
  -> rtp_common_read()

switch_core_session_write_frame()
  -> sofia_write_frame()
  -> switch_core_media_write_frame()
  -> switch_rtp_write_frame()
  -> switch_rtp_write_raw()
```

因此完整边界是：

```text
SDP match
  -> local port + advertised address
  -> codec/payload map
  -> RTP session/socket
  -> remote address
  -> ICE/RTCP/DTLS/SRTP
  -> TFLAG_RTP + TFLAG_IO
  -> switch_rtp_ready() == true
  -> RTP frame IO
```

## 8. 相关源码

- `src/mod/endpoints/mod_sofia/sofia.c` — `sofia_handle_sip_i_state`、各
  `nua_callstate_*` 分支
- `src/mod/endpoints/mod_sofia/sofia_media.c` — `sofia_media_negotiate_sdp`、
  `sofia_media_tech_media`、`sofia_media_activate_rtp`
- `src/switch_core_media.c` — `switch_core_media_negotiate_sdp`、
  `switch_core_media_choose_port`、`switch_core_media_gen_local_sdp`、
  `switch_core_media_activate_rtp`
- `src/switch_rtp.c` — `switch_rtp_request_port`、`switch_rtp_ready`、RTP read/write
- `src/mod/endpoints/mod_sofia/mod_sofia.c` — `sofia_receive_message`、endpoint IO
  callbacks
- `src/mod/endpoints/mod_sofia/mod_sofia.c` — `sofia_answer_channel`、progress/answer
  的 proxy media 与 late negotiation 路径
- [06. Workflows — Media negotiation and RTP activation](../1.architecture/05-workflows.md#media-negotiation-and-rtp-activation)
- [18. Sofia-SIP NUA — 呼叫状态机和回调](../2.signal/03-sofia-nua-call-model.md)

<!-- PKB-metadata
last_updated: 2026-09-17
commit: 4ff191a
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->
