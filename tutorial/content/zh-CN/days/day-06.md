# 第 6 天：SIP 消息、事务、对话与拆线

## 今日成果

- 能把一通 1000→1001 呼叫拆成 REGISTER 事务、INVITE 事务、对话（dialog）和拆线。
- 用 Sofia siptrace 只记录方法与状态码，并在呼叫结束后立刻关闭。
- 能对照 CHANNEL_CREATE / ANSWER / HANGUP，说明 SIP 层与 core 事件不是同一张时间表。

## 核心原理

事务（transaction）是一对请求及其响应：REGISTER+401+带凭证 REGISTER+200 是认证事务；INVITE+100/180+200 是邀请事务，200 之后的 ACK 结束 INVITE 事务，但不结束对话。对话（dialog）用 Call-ID 和两端 tag 标识已建立的会话关系。未接通用 CANCEL（或超时 487），已接通用 BYE。

core 事件描述 session 状态机：`CS_NEW` → `CS_ROUTING` → `CS_EXECUTE` → hangup。SIP 180 不等于 CHANNEL_ANSWER；RTP 开始也不等于 200 OK。排障时先标“信令阶段”，再决定要不要看媒体。

## 源码导航

- [`man/1.architecture/05-workflows.md`](../../../../man/1.architecture/05-workflows.md)：入站 SIP 与注册工作流。
- [`src/mod/endpoints/mod_sofia/sofia.c`](../../../../src/mod/endpoints/mod_sofia/sofia.c)：消息分发。
- [`src/switch_core_state_machine.c`](../../../../src/switch_core_state_machine.c)：channel 状态迁移。
- [`src/include/switch_types.h`](../../../../src/include/switch_types.h)：`CS_*` 状态枚举。
- [本课实验目录](../../../labs/day-06/)

## 源码深挖

Sofia 回调首先进入 `sofia_event_callback`，再排队到 `our_sofia_event_callback`；后者根据 `nua_event_t`、session/private data 和 SIP message 分发到 INVITE、REGISTER、BYE 等处理函数。INVITE 的入口 `sofia_handle_sip_i_invite` 会提取 Request-URI、From/To、context、dialplan 和远端 SDP，随后交给 session/dialplan/media 层。

事务、dialog、session 不是同一个对象：事务负责一次请求/响应重传语义；dialog 由 Call-ID 与 tags 标识长期会话；FreeSWITCH session/channel 承载媒体、变量和应用执行。BYE 到达后，即使 SIP 对端已经结束，也还要经过 channel hangup 和 session thread 清理，才算资源回收完成。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "sofia_event_callback|our_sofia_event_callback|sofia_handle_sip_i_invite|nua_i_bye|nua_i_invite|Call-ID" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia/sofia.c"
```

抓包时用 Call-ID、CSeq、From/To tag 和 FreeSWITCH UUID 做关联；不要只按时间戳猜测一条 200 OK 属于哪一路呼叫。

## 引导实验

前置条件：第 5 天两个分机可注册。本课只跟踪**一通**测试呼叫。

1. 打开有时限的 siptrace，随后立即拨打：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia profile internal siptrace on'
   ```

   1000 呼叫 1001，接听后说一句，再挂断。然后立刻：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia profile internal siptrace off'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'console loglevel notice'
   ```

2. 从容器日志抽出方法与状态码，做成时间表。只保留类似：

   `REGISTER → 401 → REGISTER → 200`（若本次呼叫前刚注册可跳过）  
   `INVITE → 100 → 180 → 200 → ACK`  
   `BYE → 200`

   删除 Authorization、Via、Contact、SDP 体。给每条响应标注它属于哪个事务。

3. 对照 core 侧只读观察（下一通呼叫时在振铃/接通/挂断各执行一次）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels as json' \
     | python3 -c 'import json,sys; d=json.load(sys.stdin);
print([(r.get("cid_num"), r.get("dest"), r.get("callstate")) for r in d.get("rows",[])])'
   ```

   记录 `RINGING` / `ACTIVE` 与 SIP 180/200 的先后，不要假设它们同一毫秒。

4. 再打一通**无人接听**：1001 不接，主叫取消或等超时。在笔记中标出本条路径出现的是 CANCEL 还是 480/487，而不是 BYE。

清理：确认 `siptrace off`。删除含 SDP 或 Authorization 的日志文件。

## 独立挑战

比较“接通后主叫挂断”和“振铃时主叫取消”两张时序图。指出 ACK 属于哪个事务、对话何时开始、哪条路径禁止用 BYE。禁止用“日志行数”当事务计数。

## 验收

**验收方式：半自动。时序结构可检查；抓包/trace 脱敏由学习者确认。**

- **pass**：有一张脱敏方法/状态码表；能指出 INVITE 事务与 dialog 的边界；siptrace 已关闭。
- **fail**：trace 一直开着，或报告含 Authorization/SDP。
- **unavailable**：无法产生呼叫或无法读取容器日志。

## 故障排查

- 现象：401 后没有第二次 REGISTER/INVITE → 证据：只有 Challenge → 原因：UA 未配置口令或 realm 错误 → 恢复：回到第 5 天认证，不把 nonce 写入报告。
- 现象：有 180 没有 200 → 证据：INVITE 事务未完成 → 原因：对端未接听，不是 RTP 故障 → 恢复：先完成信令，再查媒体。
- 现象：页面仍显示通话中 → 证据：BYE/HANGUP 已发生 → 原因：UA 状态机未处理拆线 → 恢复：以 `show channels` 为空为准。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 事务 | transaction | 一个请求及其响应的可靠交互范围 |
| 对话 | dialog | 由 Call-ID 与 tags 关联的已建立会话 |
| 临时响应 | provisional response | 1xx，不结束 INVITE 事务 |
| 拆线 | teardown | CANCEL 或 BYE 结束尝试/对话的过程 |
| SIP 跟踪 | siptrace | Sofia 打印信令的诊断开关，用完必须关 |
