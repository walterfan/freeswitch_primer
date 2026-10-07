# 第 28 天：Prometheus、健康与陈旧性

## 今日成果

- 阅读 `/metrics` 的 HELP/TYPE/sample，确认固定 family。
- 区分 `freeswitch_up 0`、`sessions_current NaN` 与陈旧 HEARTBEAT。
- 确认 IVR label 只有 allowlist；未知折叠为 `other`。

## 核心原理

教学服务暴露 Prometheus 文本，**不**让 Prometheus 去刮 ESL。outage 时 `freeswitch_up` 为 0，heartbeat age 继续增长或为 NaN。最后一次成功的 session 数不能在断连后继续当“当前值”。IVR 的 menu/choice 来自配置 allowlist。

## 源码导航

- [`tutorial/site/src/metrics.cpp`](../../../site/src/metrics.cpp)：`prometheus()`
- [`tutorial/deploy/prometheus.yml`](../../../deploy/prometheus.yml)
- [`tutorial/site/test/http_smoke.py`](../../../site/test/http_smoke.py)
- [`tutorial/site/src/http_server.cpp`](../../../site/src/http_server.cpp)：`/metrics`
- [本课实验目录](../../../labs/day-28/)

## 源码深挖

Prometheus 输出由 `MetricsRegistry::prometheus` 从一次 `snapshot()` 结果生成，站点 `/metrics` 只是返回这段文本；Prometheus 不直接连接 ESL。这样采集端不会把命令执行权限暴露给监控系统，也能让 HELP/TYPE/family 保持稳定。

指标语义要区分三类：`freeswitch_up` 是最近 status/连接判断的状态；session current/total 分别是 gauge/counter；heartbeat age 是新鲜度，不能因为没有新事件就重置为 0。IVR menu/choice 经过 allowlist，未知值折叠到 `other`，避免高基数标签。

```bash
rg -n "MetricsRegistry::snapshot|MetricsRegistry::prometheus|# HELP|# TYPE|heartbeat_age|bounded_ivr_choice|freeswitch_up" \
  tutorial/site/src/metrics.cpp tutorial/site/src/http_server.cpp
rg -n "scrape_configs|metrics_path|7009" tutorial/deploy/prometheus.yml tutorial/site/config/config.yaml
```

手工检查时看 HELP/TYPE、样本值和 health JSON 是否表达同一事实；“页面显示 0”不能证明依赖真的健康。

## 引导实验

前置条件：教学网站在 7009。

1. 抓取并核对 family：

   ```bash
   curl -fsS http://127.0.0.1:7009/metrics
   ```

   至少应看到：`freeswitch_up`、`freeswitch_esl_connected`、`freeswitch_sessions_current`、`freeswitch_sessions_total`、`freeswitch_sessions_capacity`、`freeswitch_calls_total`、`freeswitch_call_duration_seconds`、`freeswitch_heartbeat_age_seconds`、`freeswitch_ivr_choice_total`。

2. ESL 未启用时：`up`/`esl_connected` 应为 0，session 相关可能是 NaN。把这当成诚实 unavailable，不是“系统没通话所以是 0”。

3. 可选本机 Prometheus：

   ```bash
   prometheus --config.file=tutorial/deploy/prometheus.yml
   ```

   只刮 `127.0.0.1:7009`。不要增加 FS 主机 target。

4. 读 `metrics.cpp` 中 unknown hangup/menu 映射到 other 的代码，用笔记举例：动态 menu `hack` 不得出现新 label。

清理：停掉仅为本课启动的 Prometheus。

## 独立挑战

解释 `sessions_current=0` 与 `sessions_current=NaN` 对值班的不同含义，以及 dashboard 应显示什么。

## 验收

**验收方式：自动加半自动。**

- **pass**：metrics 文本结构正确；无 UUID/IP label。
- **fail**：断 ESL 后仍显示旧 session 为健康绿色 0。
- **unavailable**：网站未运行。

## 故障排查

- 现象：malformed heartbeat 变 0 → 证据：代码填零 → 原因：错误的默认值 → 恢复：保持 NaN/degraded。
- 现象：label 出现 UUID → 证据：scrape 文本 → 原因：normalize 泄漏 → 恢复：立刻停 scrape 并修。
- 现象：age 冻住 → 证据：时钟或 last heartbeat 未更新 → 原因：reader 停 → 恢复：第 19/27 天。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 暴露格式 | exposition | Prometheus 文本协议 |
| 陈旧 | stale | 不再代表当前依赖 |
| 标签基数 | label cardinality | 标签取值数量上限 |
| 可用性 | up | 依赖是否可达的 gauge |
