# 第 29 天：网站 Event/Metrics 面板与故障演练

## 今日成果

- 对照页面 Event、`/metrics` 和 `fs_cli`，以服务端 snapshot 为准。
- 完成一次依赖故障演练：停 ESL 或停 FS，页面显示 unavailable/degraded 而不是假零。
- 确认课程正文在依赖失败时仍可读。

## 核心原理

UI 是 snapshot 的投影。SSE 丢包时页面可能落后；值班以 `/metrics` 与 health JSON 为准。第 1 天的教学平面独立：Markdown 不依赖 Sofia。

## 源码导航

- [`tutorial/site/web/live_evidence.mjs`](../../../site/web/live_evidence.mjs)
- [`tutorial/site/web/app.mjs`](../../../site/web/app.mjs)
- [`tutorial/site/src/http_server.cpp`](../../../site/src/http_server.cpp)：health / events / metrics
- [`tutorial/site/web/preflight.mjs`](../../../site/web/preflight.mjs)
- [本课实验目录](../../../labs/day-29/)

## 源码深挖

HTTP 层把三个不同读取面分开：`/api/v1/health` 读取组件健康 snapshot，`/api/v1/events` 读取 EventHub 的事件 snapshot，`/metrics` 读取 Prometheus 文本。浏览器 `live_evidence.mjs` 是展示层，不应自己执行 FreeSWITCH 命令或推断缺失事件。

健康状态有优先级：unavailable 高于 stale，stale/degraded 高于 healthy。ESL 断开时，`MetricsRegistry::set_esl_connected(false)` 会撤销 FreeSWITCH 可用判断；最后一次成功值可以保留用于诊断，但不能继续冒充当前状态。

```bash
rg -n "CROW_ROUTE|/api/v1/health|/api/v1/events|/metrics|overall_state|ComponentState::unavailable|ComponentState::stale" \
  tutorial/site/src/http_server.cpp tutorial/site/src/health.cpp tutorial/site/src/metrics.cpp
rg -n "live_evidence|Event|metrics|health" tutorial/site/web/live_evidence.mjs tutorial/site/web/app.mjs
```

故障演练的判定不是页面是否还能打开，而是页面、health、metrics 是否分别准确表达“教学站点还活着”“ESL/FS 不可用”“数据已陈旧”。

## 引导实验

1. 正常基线：打开 `#day-29`，同时：

   ```bash
   curl -fsS http://127.0.0.1:7009/api/v1/health
   curl -fsS http://127.0.0.1:7009/metrics | head
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
   ```

   记录三者是否一致（UP vs freeswitch_up）。

2. 打一通 9196，看 Event 面板与 `calls_total` 是否在挂断后增加。听感仍单独记录。

3. **演练**：在配置里关闭 ESL 或停网站的 ESL 环境变量后重启教学服务（不要停生产）。health 中 `esl` 应变 unavailable。课程地图仍能打开 day-01 正文。

4. 恢复 ESL。不要在演练中 `unload mod_sofia`。

清理：恢复配置。关闭多余 trace。

## 独立挑战

写三行给 UI 同学：何时信页面、何时信 `/metrics`、何时信 `fs_cli`。

## 验收

**验收方式：半自动加人工。**

- **pass**：不一致时以服务端为准；演练中正文仍可读。
- **fail**：依赖失败时页面画一条漂亮的全零图还标绿。
- **unavailable**：无浏览器。

## 故障排查

- 现象：面板有事件、metrics 没有 → 证据：normalize 丢了 result → 原因：过滤器过严 → 恢复：对照 unit tests。
- 现象：metrics 有、面板无 → 证据：SSE 未订阅或 drop → 原因：第 27 天队列 → 恢复：刷新订阅。
- 现象：演练后 Sofia 没了 → 证据：误 unload → 原因：操作范围过大 → 恢复：只动教学服务。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 快照投影 | snapshot projection | UI 对服务端不可变副本的展示 |
| 故障演练 | failure drill | 故意断开依赖并恢复 |
| 诚实不可用 | honest unavailable | 缺失证据时不伪造成功 |
