# 第 26 天：status、HEARTBEAT、Channel events 与 CDR

## 今日成果

- 把 `status` 的当前 session、HEARTBEAT 的周期性新鲜度、CHANNEL_* 生命周期和 CDR 分成四种语义。
- 用一通短呼叫观察 create/answer/hangup，且不把 UUID 当 Metrics 标签。
- 理解乱序与重复 hangup 时只能结算一次。

## 核心原理

`status` 是即时 gauge 类摘要（UP、session、capacity）。HEARTBEAT 是 core 周期事件，适合算 `heartbeat_age_seconds`。CHANNEL_CREATE/DESTROY 描述 live session。CDR 在结束后提供 duration，可能早于 DESTROY。Metrics 必须按相关标识 finalize，重复 hangup 不加第二次 `calls_total`。

缺失 HEARTBEAT 字段时用 unknown/NaN，禁止填 0 假装健康。

## 源码导航

- [`tutorial/site/src/metrics.cpp`](../../../site/src/metrics.cpp)
- [`src/switch_core.c`](../../../../src/switch_core.c)：HEARTBEAT
- [`src/switch_core_state_machine.c`](../../../../src/switch_core_state_machine.c)
- [`src/mod/event_handlers/mod_event_socket/mod_event_socket.c`](../../../../src/mod/event_handlers/mod_event_socket/mod_event_socket.c)
- [本课实验目录](../../../labs/day-26/)

## 源码深挖

FreeSWITCH core 的 `send_heartbeat` 每周期构造 `SWITCH_EVENT_HEARTBEAT`，包含 uptime、版本、当前 session、最大 session 和吞吐摘要。它是进程新鲜度信号，不是某一通电话的生命周期。channel 生命周期则由 session/channel event 发布，CDR 是呼叫结束后的业务结算信息，三者时间点不同。

网站 `MetricsRegistry::record_event` 把 CREATE/ANSWER/HANGUP/DESTROY/CDR 分发到不同状态更新函数。`record_call_hangup` 用 `finalized` 防止重复 hangup 重复计数；`record_call_destroyed` 只减少当前 session gauge，不负责第二次结算。HEARTBEAT 字段解析失败应标记 degraded，而不是把未知值写成 0。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "send_heartbeat|SWITCH_EVENT_HEARTBEAT|Session-Count|Max-Sessions|SWITCH_EVENT_CHANNEL_|switch_event_fire" \
  "$FREESWITCH_SRC/src/switch_core.c" "$FREESWITCH_SRC/src/switch_core_session.c" \
  "$FREESWITCH_SRC/src/switch_event.c"
rg -n "record_event|record_call_hangup|record_call_destroyed|record_cdr|finalized" tutorial/site/src/metrics.cpp tutorial/site/include/metrics.h
```

先画事件时间线，再决定哪个事件更新 gauge、哪个事件更新 counter；不能把所有事件都当成同一种“当前状态”。

## 引导实验

前置条件：第 2 天 status；第 19 天 ESL 概念。

1. 安全摘要：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
   ```

   只记录是否 UP、当前 session 数量级、uptime 存在。不要抄完整主机名到公开文档。

2. 打 9196 数秒后挂断。期间三次 `show channels`：振铃/回声/结束后。对照页面若已连 ESL，看 create/answer/hangup 顺序。

3. 若启用 `/metrics`：

   ```bash
   curl -fsS http://127.0.0.1:7009/metrics | grep -E 'freeswitch_sessions|freeswitch_calls|heartbeat'
   ```

   比较通话前后 `sessions_current` 与 `calls_total`。label 只应有 `result` 这类有限值。

4. 阅读 `metrics.cpp` 中 HEARTBEAT 缺失时输出 `NaN` 的分支，用自己的话写进笔记。

清理：挂断。不要打开 CDR 模块把号码写入外部数据库。

## 独立挑战

四格表：status 当前值、heartbeat freshness、call counter、duration。各写“何时更新 / 何时不能当现状”。

## 验收

**验收方式：自动（parser tests）加 lab。**

- **pass**：能区分四种来源；metrics 无 UUID label。
- **fail**：用旧 status 会话数冒充当前健康。
- **unavailable**：网站未开 ESL，则只完成 fs_cli 部分。

## 故障排查

- 现象：无 HEARTBEAT → 证据：age NaN 或 degraded → 原因：事件连接未订阅 HEARTBEAT → 恢复：检查 observer 订阅列表。
- 现象：calls_total 跳两下 → 证据：重复 hangup → 原因：未 finalize → 恢复：读 correlation 逻辑。
- 现象：CDR 早于 destroy → 证据：时序交叉 → 原因：允许的，后续事件只补充。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 心跳 | HEARTBEAT | 周期性活性事件 |
| 仪表 | gauge | 表示当前值 |
| 计数器 | counter | 只增不减的累计 |
| 通话详单 | CDR | 结束后的呼叫记录 |
