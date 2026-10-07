# 第 27 天：ESL Collector、有界快照与重复事件

## 今日成果

- 指出 EventHub 的 `recent_events` 与 `client_queue` 上限来自网站 YAML。
- 用现有 unit tests 覆盖：重复 hangup、乱序、队列溢出与 drop 计数。
- 说明慢浏览器不能阻塞 ESL reader。

## 核心原理

observer 把原始事件变成内部结构，Metrics 与 EventHub 各拿一份。snapshot 在 mutex 下拷贝后只读返回。每个 SSE 客户端有队列上限；溢出增加 drop 并继续收新事件。呼叫 correlation 另有上限 `call_correlations`。

默认见 [`tutorial/site/config/config.yaml`](../../../site/config/config.yaml)：recent 1000、queue 256、correlations 4096。

## 源码导航

- [`tutorial/site/src/esl_observer.cpp`](../../../site/src/esl_observer.cpp)：`EventHub`
- [`tutorial/site/include/config.h`](../../../site/include/config.h)
- [`tutorial/site/test/unit_tests.cpp`](../../../site/test/unit_tests.cpp)
- [`tutorial/site/src/metrics.cpp`](../../../site/src/metrics.cpp)
- [本课实验目录](../../../labs/day-27/)

## 源码深挖

`EventHub::publish` 先把原始事件规范化，再写入 bounded recent ring，并把副本推给各客户端；SSE 客户端拥有自己的 bounded queue。慢客户端只会丢自己的旧事件并增加 drop 计数，不应阻塞 ESL reader。`RecentEventRing::snapshot` 在锁内复制容器，调用者在锁外序列化。

呼叫 correlation 也有上限。`MetricsRegistry::prune_correlations` 从最老的顺序队列淘汰条目；淘汰未完成 correlation 时会修正当前 session gauge，但不会凭空增加 completed counter。重复 hangup 由 `finalized` 拦截，乱序事件则通过“先见到什么就补建状态”继续收敛。

```bash
rg -n "EventHub::publish|RecentEventRing::snapshot|client_queue|drop|prune_correlations|finalized|try_emplace" \
  tutorial/site/src/esl_observer.cpp tutorial/site/src/metrics.cpp \
  tutorial/site/include/esl_observer.h tutorial/site/include/metrics.h
```

验证时分别制造：重复 hangup、DESTROY 先到、SSE 客户端暂停、correlation 达上限；每种情况都检查 reader 仍能接收后续事件。

## 引导实验

1. 跑网站单元测试（在 tutorial/site 构建树）：

   ```bash
   cmake -S tutorial/site -B tutorial/site/build
   cmake --build tutorial/site/build --target tutorial-site-unit-tests
   ctest --test-dir tutorial/site/build -R tutorial-site-unit --output-on-failure
   ```

   若目标名不同，用 `ctest --test-dir tutorial/site/build --output-on-failure` 看列表。重点看 EventHub 与 metrics 用例。

2. 打开 `unit_tests.cpp` 中 `recent_events: 10, client_queue: 5` 的夹具，理解测试如何制造 overflow。不要把测试里的假事件当生产流量。

3. 实时观察（可选）：打开 Event 面板后暂停浏览器（断点），同时打几通短呼叫，看 drop 或页面提示，而 FS 侧 `status` 仍 UP。

清理：去掉断点。不要把 limits 改到无上限。

## 独立挑战

给定 recent=1000、queue=256，估算最坏内存项数量级，并说明 correlation 与 recent event 为何寿命不同。

## 验收

**验收方式：自动。**

- **pass**：unit tests 通过；能解释 drop 不阻塞 reader。
- **fail**：为了“不丢事件”去掉有界队列。
- **unavailable**：站点未构建。

## 故障排查

- 现象：重复 call total → 证据：两次 hangup → 原因：finalize 标志 → 恢复：读 metrics 关联代码。
- 现象：snapshot 半更新 → 证据：字段互相矛盾 → 原因：锁外读内部 map → 恢复：只返回拷贝。
- 现象：客户端卡住 → 证据：SSE 停 → 原因：无限队列背压 → 恢复：有界+drop 通知。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 收集器 | collector | 聚合 ESL 信号的组件 |
| 有界保留 | bounded retention | 历史硬上限 |
| 丢弃 | drop | 超出队列时丢掉旧/新事件并计数 |
| 乱序 | out of order | 到达顺序≠发生顺序 |
