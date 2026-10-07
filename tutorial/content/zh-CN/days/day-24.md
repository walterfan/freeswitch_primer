# 第 24 天：Custom event、有界计数器、锁与并发

## 今日成果

- 指出事件字段：`Menu`、`Choice`、`Unique-ID`、时间戳；浏览器不得展示 Unique-ID。
- 理解计数器有上限：菜单≤8，选择≤32，名字字符集受限。
- 用单元测试而不是“多开 100 路呼叫”来证明 mutex 保护。

## 核心原理

成功路径 `switch_event_create_subclass` + `switch_event_fire`。计数更新与查找在 `globals.mutex` 内。配置阶段拒绝重复 menu/choice、空菜单、非法字符。这些限制防止 Metrics 标签爆炸。

并发测试在 `tutorial/module/mod_tutorial/test/`，不需要 SIP。

## 源码导航

- [`tutorial/module/mod_tutorial/mod_tutorial.c`](../../../module/mod_tutorial/mod_tutorial.c)
- [`tutorial/module/mod_tutorial/mod_tutorial_logic.c`](../../../module/mod_tutorial/mod_tutorial_logic.c)
- [`tutorial/module/mod_tutorial/test/test_mod_tutorial.c`](../../../module/mod_tutorial/test/test_mod_tutorial.c)
- [`tutorial/tests/module_integration.sh`](../../../tests/module_integration.sh)
- [本课实验目录](../../../labs/day-24/)

## 源码深挖

教程模块把配置限制成固定大小数组：菜单和 choice 在 load 时校验字符集、长度、重复项和总数；运行时只在数组内线性查找。这里的线性扫描是教学用的有界实现，复杂度上限由 `TUTORIAL_MAX_CHOICES` 固定，不会随外部输入无限增长。

锁覆盖“查找 + 更新计数 + 复制选中值”的完整临界区；解锁后才做 channel 写入、日志和 `switch_event_fire`。事件分发可能触发其他线程/消费者，不能在持有全局 mutex 时做不可控的外部工作。

```bash
rg -n "TUTORIAL_MAX_|tutorial_valid_|tutorial_find_choice|switch_mutex_lock|switch_mutex_unlock|switch_event_fire" \
  tutorial/module/mod_tutorial/mod_tutorial_logic.h \
  tutorial/module/mod_tutorial/mod_tutorial_logic.c \
  tutorial/module/mod_tutorial/mod_tutorial.c
```

单元测试应验证边界值、非法输入、重复配置和多线程后的总计数；“跑几路电话没崩”不能证明计数没有竞态。

## 引导实验

1. 跑模块单元测试：

   ```bash
   ctest --test-dir tutorial/module/mod_tutorial/build -R mod-tutorial-unit --output-on-failure
   ```

2. 阅读 `tutorial_valid_menu` / `tutorial_valid_choice` 与 `TUTORIAL_MAX_*`。尝试在**副本** XML 里加第 9 个菜单（不要提交），load 应失败。用完删除副本。

3. 可选集成：

   ```bash
   TUTORIAL_INTEGRATION=1 \
   TUTORIAL_MODULE="$PWD/tutorial/module/mod_tutorial/build/mod_tutorial.so" \
   TUTORIAL_MODULE_CONFIG="$PWD/tutorial/module/mod_tutorial/mod_tutorial.conf.xml" \
   ctest --test-dir tutorial/module/mod_tutorial/build -R mod-tutorial-integration --output-on-failure
   ```

   需要本机 fs_cli 能连隔离 FS。失败时看脚本是否 SKIP。

4. 一次成功 IVR 后，确认 Event 面板只有 Menu/Choice 摘要。

清理：不要把超限 XML 留在容器 autoload。

## 独立挑战

计算 8×平均 4 个 choice 的最大时间序列标签数，说明为什么未知输入必须进 `invalid` 而不是动态新 key。

## 验收

**验收方式：自动。**

- **pass**：单元测试通过；能复述事件在非法路径不发送。
- **fail**：为了“灵活”去掉上限。
- **unavailable**：未构建测试。

## 故障排查

- 现象：load 报 duplicate menu → 证据：conf 重复 → 原因：复制粘贴 XML → 恢复：保持两菜单五选择。
- 现象：并发测试挂起 → 证据：死锁 → 原因：锁顺序错误 → 恢复：对照现有 mutex 只包计数器。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 有界计数器 | bounded counter | 预定义 key 上的累计值 |
| 互斥锁 | mutex | 保护计数器与配置快照 |
| 事件子类 | event subclass | CUSTOM 事件的名字 |
