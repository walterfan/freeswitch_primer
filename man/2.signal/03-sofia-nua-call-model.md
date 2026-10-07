# 18. Sofia-SIP NUA：呼叫状态机和回调

<!-- maintained-by: human+ai -->

NUA 的关键不是“调用 `nua_invite()` 发一个包”，而是理解：一次呼叫会经过多
个状态，应用通过 callback 观察结果并决定下一步。官方把这个模型分为 client
side、server side 和会话修改/释放三部分。[NUA Call Model](https://sofia-sip.sourceforge.net/refdocs/nua/nua_call_model.html)

## 1. Callback 先看懂

典型 callback 形状如下：

```c
void app_callback(nua_event_t event,
                  int status,
                  char const *phrase,
                  nua_t *nua,
                  nua_magic_t *magic,
                  nua_handle_t *nh,
                  nua_hmagic_t *hmagic,
                  sip_t const *sip,
                  tagi_t tags[])
{
    switch (event) {
    case nua_r_invite:
        /* 出站 INVITE 的响应 */
        break;
    case nua_i_invite:
        /* 入站 INVITE */
        break;
    case nua_i_state:
        /* 呼叫状态或 SDP 状态变化 */
        break;
    default:
        break;
    }
}
```

### 1.1 事件命名规律

| 前缀/事件 | 含义 | 例子 |
|---|---|---|
| `nua_r_...` | 应用发出的请求收到响应/结果 | `nua_r_invite`、`nua_r_bye` |
| `nua_i_...` | 收到对端请求或状态指示 | `nua_i_invite`、`nua_i_message` |
| `nua_i_state` | 呼叫状态或媒体协商状态改变 | `NUTAG_CALLSTATE_REF()` |
| `nua_r_shutdown` | 优雅关闭完成或失败 | `su_root_break()` |

`status` 是 SIP 状态码或 NUA 操作结果，`phrase` 是可读原因，`nh` 指向相关
handle，`sip` 是收到的 SIP 消息，`tags` 包含额外的结果和状态。

## 2. 呼叫状态总图

```text
                         INVITE / 100
                       +-------------+
                       |   RECEIVED  |
                       +------+------+
                              |
                         18X / nua_respond
                              v
                       +-------------+
                       |    EARLY    |
                       +------+------+
                              | 2XX / nua_respond
                              v
                       +-------------+
                       |  COMPLETED  |
                       +------+------+
                              | ACK
                              v
                       +-------------+
                       |    READY    | <----> re-INVITE / UPDATE
                       +------+------+
                              | BYE
                              v
                       +-------------+
                       | TERMINATING |
                       +------+------+
                              | BYE final response
                              v
                       +-------------+
                       | TERMINATED  |
                       +-------------+
```

出站呼叫常见状态是：

```text
INIT -> CALLING -> PROCEEDING -> COMPLETING -> READY -> TERMINATING -> TERMINATED
```

入站呼叫常见状态是：

```text
INIT -> RECEIVED -> EARLY -> COMPLETED -> READY -> TERMINATING -> TERMINATED
```

状态名来自 `nua_callstate`，在 `nua_i_state` 事件的
`NUTAG_CALLSTATE()` 中取得。可以用 `nua_callstate_name()` 转成可读字符串。

## 3. 出站呼叫

### 3.1 创建 handle 并发送 INVITE

```c
call_t *call = su_zalloc(app->home, sizeof *call);
if (!call) {
    return;
}

call->handle = nua_handle(app->nua,
                          call,
                          SIPTAG_TO_STR("<sip:1001@example.com>"),
                          TAG_END());
if (!call->handle) {
    su_free(app->home, call);
    return;
}

nua_invite(call->handle,
           SOATAG_USER_SDP_STR(local_sdp),
           SIPTAG_SUBJECT_STR("hello"),
           TAG_END());
```

调用 `nua_invite()` 后不要同步等待 200 OK；结果会通过 `nua_r_invite` 和
`nua_i_state` 异步返回。

### 3.2 处理响应

```c
static void on_r_invite(int status,
                        nua_handle_t *nh,
                        tagi_t tags[])
{
    if (status < 200) {
        /* 100 / 180 / 183 等临时响应 */
        return;
    }

    if (status == 200) {
        /* 默认通常自动 ACK；关闭 auto-ack 时必须显式 ACK */
        nua_ack(nh, TAG_END());
        return;
    }

    /* 3xx-6xx：呼叫失败，底层事务负责错误响应的 ACK */
}
```

官方 call model 说明：如果初始 INVITE 携带 SDP offer，通常在 2xx 中收到
answer；发送 ACK 后 Offer/Answer 完成，媒体进入 active 状态。

### 3.3 读取呼叫和 SDP 状态

```c
static void on_state(nua_handle_t *nh, tagi_t tags[])
{
    nua_callstate_t state = nua_callstate_init;
    sdp_session_t *remote_sdp = NULL;
    sdp_session_t *local_sdp = NULL;

    tl_gets(tags,
            NUTAG_CALLSTATE_REF(state),
            SOATAG_REMOTE_SDP_REF(remote_sdp),
            SOATAG_LOCAL_SDP_REF(local_sdp),
            TAG_END());

    printf("state=%s\n", nua_callstate_name(state));
}
```

实际项目中应根据当前 Sofia-SIP 版本的头文件类型使用对应的 `SOATAG_*`
引用宏；重点是从 `nua_i_state` 同时读取：

| Tag | 含义 |
|---|---|
| `NUTAG_CALLSTATE()` | 当前 NUA 呼叫状态 |
| `NUTAG_OFFER_SENT()` | 本地是否发送过 offer |
| `NUTAG_ANSWER_SENT()` | 本地是否发送过 answer |
| `NUTAG_OFFER_RECV()` | 是否收到远端 offer |
| `NUTAG_ANSWER_RECV()` | 是否收到远端 answer |
| `SOATAG_LOCAL_SDP()` | 本地 SDP |
| `SOATAG_REMOTE_SDP()` | 远端 SDP |
| `SOATAG_ACTIVE_AUDIO()` | 音频媒体是否 active |
| `SOATAG_ACTIVE_VIDEO()` | 视频媒体是否 active |

## 4. 入站呼叫

收到新 INVITE 时，NUA 会创建 handle 并触发 `nua_i_invite`。应用可以振铃、拒绝
或接听：

```c
static void on_i_invite(nua_handle_t *nh,
                        sip_t const *sip,
                        tagi_t tags[])
{
    /* 振铃：可选择发送早期响应和早期 SDP */
    nua_respond(nh,
                SIP_180_RINGING,
                TAG_END());

    /* 接听：示例使用本地 SDP 作为 answer */
    nua_respond(nh,
                SIP_200_OK,
                SOATAG_USER_SDP_STR(local_sdp),
                TAG_END());
}
```

常见决策：

| 业务决定 | NUA 动作 |
|---|---|
| 正在振铃 | `nua_respond(nh, SIP_180_RINGING, ...)` |
| 发送早期媒体 | `nua_respond()` 携带 SDP |
| 接听 | `nua_respond(nh, SIP_200_OK, ...)` |
| 忙线 | `nua_respond(nh, SIP_486_BUSY_HERE, ...)` |
| 拒绝 | `nua_respond(nh, 4xx/5xx/6xx, ...)` |

入站侧收到 ACK 后进入 `READY`；如果 200 OK 发出后迟迟收不到 ACK，NUA 会在
超时后发送 BYE 终止呼叫。

## 5. 取消、挂断和竞争条件

### 5.1 `nua_cancel()`

```c
nua_cancel(call->handle, TAG_END());
```

适用于出站呼叫仍处于 `CALLING` 或 `PROCEEDING` 阶段。常见报文是：

```text
CANCEL -> 200 OK
INVITE -> 487 Request Terminated
```

但存在竞态：对端可能在收到 CANCEL 前已经返回 200 OK。此时呼叫反而建立，
应用需要在确认成功后调用 `nua_bye()`。

### 5.2 `nua_bye()`

```c
nua_bye(call->handle, TAG_END());
```

已建立对话使用 BYE。早期对话进入 `PROCEEDING` 后也可以使用 BYE；在尚未
建立对话时，NUA 可能把应用的结束动作转换为 CANCEL。

收到 `nua_r_bye` 的最终响应后再销毁 handle：

```c
if (event == nua_r_bye && status >= 200) {
    nua_handle_destroy(nh);
    su_free(app->home, call);
}
```

不要在仍有未完成 NUA 操作时提前释放 `hmagic` 指向的业务对象。

## 6. 会话修改

进入 `READY` 后，re-INVITE 或 UPDATE 可以用于：

- hold/resume：修改 `a=sendonly`、`a=recvonly`、`a=inactive` 或 `a=sendrecv`。
- 增加或删除媒体流。
- 更新编解码和媒体地址。
- 在早期对话中完成额外 Offer/Answer。

```c
nua_update(call->handle,
           SOATAG_USER_SDP_STR(updated_sdp),
           TAG_END());
```

NUA API Overview 将 `nua_update()`、`nua_prack()` 和 Offer/Answer tags 归在
呼叫模型扩展中；`100rel`、precondition 和 early media 需要同时检查对端的
`Supported`/`Require`。[NUA API Overview — call model extensions](https://sofia-sip.sourceforge.net/refdocs/nua/nua_api_overview.html)

## 7. 生命周期清理

```text
READY
  ├─ 收到 BYE       -> nua_i_bye -> 200 -> TERMINATED
  ├─ 调用 nua_bye   -> nua_r_bye -> final response -> TERMINATED
  ├─ re-INVITE 失败 -> 根据错误判断 graceful 或 fatal
  └─ 媒体协商失败   -> ACK 后通常自动 BYE
```

只有在确认呼叫进入 `TERMINATED`、且相关最终响应已处理后，才释放呼叫上下文。

## 参考资料

- [NUA Call Model](https://sofia-sip.sourceforge.net/refdocs/nua/nua_call_model.html)
- [NUA API Overview](https://sofia-sip.sourceforge.net/refdocs/nua/nua_api_overview.html)
- [Sofia-SIP NUA main page](https://sofia-sip.sourceforge.net/refdocs/nua/index.html)

<!-- PKB-metadata
last_updated: 2026-09-17
commit: 4ff191a
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->
