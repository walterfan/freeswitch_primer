# 第 22 天：tutorial_metrics API 与一致快照

## 今日成果

- 调用 `tutorial_metrics` 与 `tutorial_metrics json`，确认字段稳定。
- 证明只读 API 不会增加 invocation 计数。
- 非法参数只返回 USAGE。

## 核心原理

API 在锁内复制 `tutorial_counter_state_t`，锁外格式化。text/json 包含 version、uptime、invocations、invalid 以及配置中每个 menu/choice 的计数。网页和 ESL 都不应执行返回文本。

## 源码导航

- [`tutorial/module/mod_tutorial/mod_tutorial.c`](../../../module/mod_tutorial/mod_tutorial.c)：`tutorial_metrics_api`
- [`tutorial/module/mod_tutorial/mod_tutorial_logic.c`](../../../module/mod_tutorial/mod_tutorial_logic.c)
- [`src/include/switch_apr.h`](../../../../src/include/switch_apr.h)
- [本课实验目录](../../../labs/day-22/)

## 源码深挖

`tutorial_metrics_api` 有意把临界区压到最小：锁内只复制 `globals.counters` 和计算 uptime，解锁后由 `tutorial_format_metrics` 写 text/JSON。这样 `fs_cli -x 'tutorial_metrics'` 不会长时间阻塞 IVR choice，也不会因格式化速度影响计数更新。

API 的只读性来自调用路径：它没有递增 invocation，也不接受任意表达式；参数只允许空、`text` 或 `json`，其余返回 `-USAGE`。`tutorial_format_metrics` 使用固定数组上限和固定字段，避免把用户输入变成无限增长的指标名。

```bash
rg -n "tutorial_metrics_api|tutorial_format_metrics|switch_mutex_lock|switch_mutex_unlock|SWITCH_ADD_API" \
  tutorial/module/mod_tutorial/mod_tutorial.c \
  tutorial/module/mod_tutorial/mod_tutorial_logic.c
```

可以在调用 API 前后各执行一次 `tutorial_metrics json`，只比较 invocation/invalid/choice 计数；如果查询本身改变业务计数，说明读写边界被破坏。

## 引导实验

前置条件：`mod_tutorial` 已 load。

1. 取基线：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'tutorial_metrics'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'tutorial_metrics json'
   ```

   再执行两次同样命令。`invocations` 不得因 API 调用增加。

2. 非法选项：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'tutorial_metrics xml'
   ```

   预期 `-USAGE: tutorial_metrics [text|json]`，计数不变。

3. 用应用增加一次计数（第 23 天会系统做；这里可用）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'tutorial_ivr_metric main 1'
   ```

   注意：API 形式的 `tutorial_ivr_metric` 若未作为 CLI API 注册，这条会失败。模块只把 `tutorial_ivr_metric` 注册为 **application**。正确做法是在呼叫中执行，或：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'originate {ignore_early_media=true}loopback/9000@tutorial &park'
   ```

   然后按键。比较 json 中 `main`/`1` 计数。

4. 用 `python3 -m json.tool` 验证 json 可解析，记录 version 字段存在即可。

清理：unload 会清零内存计数；仅在本实验容器允许。

## 独立挑战

写 10 行检查器：解析 json，拒绝缺 version 或非对象。说明为什么不能 `eval` 返回值。

## 验收

**验收方式：自动（logic tests）加 ESL。**

- **pass**：text/json 字段一致；USAGE 不增加计数。
- **fail**：把 application 名当 API 反复打 CLI 造成误解却不记录。
- **unavailable**：模块未 load。

## 故障排查

- 现象：输出截断 → 证据：json 不完整 → 原因：buffer 或 choice 超上限 → 恢复：配置最多 8 菜单 32 选择。
- 现象：USAGE 改变计数 → 证据：invocations 上升 → 原因：解析错误走到 record → 恢复：读 `tutorial_metrics_api` 前半段，应在计数前返回。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 快照 | snapshot | 锁内复制的完整计数 |
| 只读 API | read-only API | 不改变模块状态的查询 |
| 调用次数 | invocations | application 被调用的累计次数 |
