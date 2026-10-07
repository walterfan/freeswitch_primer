# 第 20 天：C++ ESL 客户端跟踪浏览器到 IVR 的通话

## 今日成果

- 把一通浏览器→9000（或 loopback 9000）的生命周期对上 ESL 事件顺序。
- 使用教学页 Event 面板或 `/api/v1/events`，只记录脱敏字段。
- 理解站点进程是观察者：挂掉网站不应挂断通话。

## 核心原理

建议事件顺序（可能乱序，第 27 天处理）：`CHANNEL_CREATE` → `CHANNEL_ANSWER` → DTMF/`CUSTOM tutorial::ivr_choice` → `CHANNEL_HANGUP` → `CHANNEL_DESTROY`。HEARTBEAT 穿插其中，与呼叫无关。

C++ 服务在 [`esl_observer.cpp`](../../../site/src/esl_observer.cpp) 订阅并规范化，经 EventHub 推到 SSE。浏览器 [`live_evidence.mjs`](../../../site/web/live_evidence.mjs) 只展示安全字段。网站崩溃时 FreeSWITCH 会话继续，直到 UA BYE。

## 源码导航

- [`tutorial/site/src/esl_observer.cpp`](../../../site/src/esl_observer.cpp)
- [`tutorial/site/web/live_evidence.mjs`](../../../site/web/live_evidence.mjs)
- [`tutorial/tests/module_integration.sh`](../../../tests/module_integration.sh)：`loopback/9000@tutorial` 与 CUSTOM 事件
- [`tutorial/module/mod_tutorial/mod_tutorial.c`](../../../module/mod_tutorial/mod_tutorial.c)：`tutorial::ivr_choice`
- [本课实验目录](../../../labs/day-20/)

## 源码深挖

网站观察者应当是旁路消费者：ESL reader 接收事件，转换为内部 `NormalizedEvent`，再由 EventHub/metrics 各自保存；HTTP/SSE 客户端只读取 snapshot。FreeSWITCH session 的主循环仍在 `switch_core_session_thread`，网站进程退出不会调用 hangup，所以观察者故障不应结束通话。

事件顺序不是严格事务日志。`CHANNEL_CREATE`、`CHANNEL_ANSWER`、CUSTOM、`CHANNEL_HANGUP` 和 `CHANNEL_DESTROY` 可能跨线程到达，HEARTBEAT 还会穿插其中。关联应使用受限的内部 key 和状态机，展示层只保留脱敏字段。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_core_session_thread|SWITCH_EVENT_CHANNEL_CREATE|SWITCH_EVENT_CHANNEL_ANSWER|SWITCH_EVENT_CHANNEL_HANGUP|SWITCH_EVENT_CHANNEL_DESTROY|switch_event_fire" \
  "$FREESWITCH_SRC/src/switch_core_session.c" "$FREESWITCH_SRC/src/switch_event.c" \
  "$FREESWITCH_SRC/src/mod/event_handlers/mod_event_socket/mod_event_socket.c"
rg -n "record_event|NormalizedEvent|EventHub|recent_events|client_queue" tutorial/site/src tutorial/site/include
```

验收时同时关闭网页、ESL reader 和 FreeSWITCH，分别观察“通话”“事件”“健康”三种状态；它们必须独立显示，而不是一处失败全都归零。

## 引导实验

前置条件：第 18 天 tutorial context；第 19 天 ESL 对网站可用则走面板，否则用容器内 fs_cli 事件（仅本机）。

1. 网站健康：

   ```bash
   curl -fsS http://127.0.0.1:7009/api/v1/health
   ```

   `esl` 若 unavailable，本课用 `fs_cli` 的 `/event` 交互不作为强制；改为通话前后 `show channels` 快照。

2. 浏览器或 loopback 进入 9000，按 `1` 再 `1`。同时观察 Event 面板。记录：create → answer →（可选 custom 事件）→ hangup。自定义事件需要 `mod_tutorial` 已 load。

3. 通话中停止教学网站进程，确认软电话/浏览器通话未立即断开。再启动网站。这证明观察平面可分离。

4. 对照集成脚本的事件字段：`Menu`、`Choice`。笔记只写 `submenu/1` 这类 allowlist 值。

清理：挂断。不要保存 SSE 原始流。

## 独立挑战

给下一开发者写复现步骤：如何在**不**打开 8021 到公网的前提下，把 IVR 选择对上一次通话。列出证据来自 FS、ESL、网站哪一层。

## 验收

**验收方式：半自动加人工。**

- **pass**：有一条脱敏事件时间线；网站重启不作为挂断手段。
- **fail**：把 HEARTBEAT 当成挂断。
- **unavailable**：ESL 与模块都未启用，只能完成 channels 快照。

## 故障排查

- 现象：有通话无事件 → 证据：health.esl unhealthy → 原因：网站未连 ESL → 恢复：第 19 天。
- 现象：有 DTMF 无 custom 事件 → 证据：模块未 load 或选择非法 → 原因：第 21–23 天 → 恢复：先 `module_exists mod_tutorial`。
- 现象：网站停、通话也停 → 证据：误把媒体接到 Crow → 原因：架构理解错误 → 恢复：回到第 1 天四平面。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 观察者 | observer | 不在媒体路径上的 ESL 客户端 |
| 自定义事件 | custom event | `CUSTOM tutorial::ivr_choice` |
| 服务端推送 | SSE | 网站把脱敏事件推到浏览器 |
| 关联 | correlation | 用内部 id 把事件连成一通电话 |
