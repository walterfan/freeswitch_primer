# 第 23 天：tutorial_ivr_metric 应用与 channel 变量

## 今日成果

- 在 live channel 上验证成功路径变量：`tutorial_ivr_recorded=true` 以及 menu/choice。
- 验证非法输入：recorded 为 false，error 为 `invalid_menu_or_choice`，且不发 CUSTOM 事件。
- 确认每次调用会先清掉旧的 menu/choice。

## 核心原理

`tutorial_ivr_metric_app` 要求恰好两个 token：allowlist 菜单名 + 单字符 `0-9/*/#`。成功：计数、channel 变量、`CUSTOM tutorial::ivr_choice`。失败：`invalid++`，设 error，**不**发事件。这防止 DTMF 被当成命令。

## 源码导航

- [`tutorial/module/mod_tutorial/mod_tutorial.c`](../../../module/mod_tutorial/mod_tutorial.c)：`tutorial_ivr_metric_app`
- [`tutorial/module/mod_tutorial/mod_tutorial.conf.xml`](../../../module/mod_tutorial/mod_tutorial.conf.xml)
- [`tutorial/module/mod_tutorial/tutorial-ivr.xml`](../../../module/mod_tutorial/tutorial-ivr.xml)
- [`src/switch_channel.c`](../../../../src/switch_channel.c)
- [本课实验目录](../../../labs/day-23/)

## 源码深挖

`tutorial_ivr_metric_app` 先清空旧的 channel variables，再把 data 按空格拆成最多三个 token；只有恰好两个 token 才进入 `tutorial_find_choice`。查找和计数在 `globals.mutex` 内完成，解锁后才写 channel variable 和创建 CUSTOM event。这样失败请求不能继承上一次成功选择，也不能把任意长字符串带入事件或统计标签。

成功事件的 `Menu`、`Choice` 来自已经通过配置 allowlist 的拷贝；`Unique-ID` 只用于服务端关联。失败路径只增加 invalid、设置 `tutorial_ivr_error`，不会发 `tutorial::ivr_choice`。

```bash
rg -n "tutorial_ivr_metric_app|tutorial_find_choice|switch_channel_set_variable|switch_event_create_subclass|switch_event_fire" \
  tutorial/module/mod_tutorial/mod_tutorial.c \
  tutorial/module/mod_tutorial/mod_tutorial_logic.c
```

测试顺序建议是：先成功 `main 1`，再同一 channel 发送非法 menu/choice，再检查 menu/choice 已清空且没有第二个 CUSTOM event。

## 引导实验

前置条件：模块 load，tutorial context 可用。

1. originate 并保持（第二终端观察）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'originate {ignore_early_media=true}loopback/9000@tutorial &park'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels'
   ```

2. 对 A-leg UUID 执行应用。`uuid_broadcast` 按空格分参数，应用参数里的空格必须写成 `^^`（笔记只留短 UUID）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'uuid_broadcast <UUID> tutorial_ivr_metric::main^^1 aleg'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'uuid_getvar <UUID> tutorial_ivr_recorded'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'uuid_getvar <UUID> tutorial_ivr_menu'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'uuid_getvar <UUID> tutorial_ivr_choice'
   ```

   预期 true / main / 1。若 broadcast 无效，改走 IVR 按键，以按键与变量为准。

3. 非法调用：`uuid_broadcast <UUID> tutorial_ivr_metric::nope^^1 aleg`。读取 `tutorial_ivr_error`，确认 recorded 为 false。对比 `tutorial_metrics json` 的 invalid 增加、choice 不变。

4. 再发一次合法 `submenu 2`，确认旧的 `main/1` 被覆盖而不是拼接。

清理：`uuid_kill` 或 `hupall`。

## 独立挑战

描述“上一菜单的 choice 残留导致下一菜单误匹配”的场景，对照源码里先把 recorded 设为 false 的顺序说明如何避免。

## 验收

**验收方式：半自动。**

- **pass**：合法/非法变量行为可复述；非法不产生自定义事件（可用第 20 天面板或集成脚本）。
- **fail**：非法输入被当成新 Prometheus label（第 28 天会再挡一层）。
- **unavailable**：无法 broadcast 且无法按键。

## 故障排查

- 现象：recorded 一直 true → 证据：失败后变量未清 → 原因：看错通道或未执行到 app → 恢复：读函数开头的 set_variable。
- 现象：额外参数被接受 → 证据：argc==3 仍成功 → 原因：解析错误 → 恢复：源码要求 argc==2。
- 现象：broadcast 无效果 → 证据：应用名或 UUID 错 → 原因：会话已 hangup → 恢复：show channels。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 拨号计划应用 | application | 在 channel 上执行的模块函数 |
| 变量清理 | variable cleanup | 新一次处理前移除旧状态 |
| 广播 | uuid_broadcast | 向已存在 session 投递应用 |
