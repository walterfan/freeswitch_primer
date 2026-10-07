# 第 17 天：DTMF 收集、超时、重试与非法输入

## 今日成果

- 读懂 `read` 应用的参数：最小/最大位数、提示、变量名、超时、终止键。
- 用 vanilla `5000` 或教程 `read` 观察：按键成功、超时、非法长度。
- 确认非法输入不会变成 shell 命令或 ESL 指令。

## 核心原理

[`read`](../../../../src/mod/applications/mod_dptools/mod_dptools.c) 语法类似：`min max prompt var timeout terminator`。教程 IVR 使用：

```text
read 1 1 'tone_stream://%(1000,0,350,440)' tutorial_main_digit 5000 #
```

只收 1 位，5 秒超时，`#` 终止。超时后变量为空，后续 nested condition 不匹配，呼叫走到 hangup。这是安全默认：未选择不等于选择了 `1`。

DTMF 必须先按第 9 天到达 RTP telephone-event，`read` 才能看到。声音包缺失时 `5000`/`demo_ivr` 可能失败，优先用 tone_stream 或 9196 验证 RTP，再用本课逻辑。

## 源码导航

- [`tutorial/module/mod_tutorial/tutorial-ivr.xml`](../../../module/mod_tutorial/tutorial-ivr.xml)：`read` 行。
- [`src/mod/applications/mod_dptools/mod_dptools.c`](../../../../src/mod/applications/mod_dptools/mod_dptools.c)
- [`conf/vanilla/dialplan/default.xml`](../../../../conf/vanilla/dialplan/default.xml)：`ivr_demo` 5000
- [本课实验目录](../../../labs/day-17/)

## 源码深挖

`read_function` 最终把提示播放和数字采集交给 input callback/DTMF 队列；RTP 层先把 telephone-event 解码并放入队列，应用层再根据最小/最大位数、timeout 和 terminator 决定成功、超时或非法长度。超时返回时变量应保持为空，不能把默认值伪装成用户输入。

“非法输入不会执行命令”来自边界设计：DTMF 只被当作 channel data，拨号计划再用 XML condition 做有限匹配。不要把收集到的字符串拼成 `api`、shell 或文件路径。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "SWITCH_STANDARD_APP\(read_function\)|switch_rtp_dequeue_dtmf|switch_rtp_has_dtmf|terminator|timeout" \
  "$FREESWITCH_SRC/src/mod/applications/mod_dptools/mod_dptools.c" \
  "$FREESWITCH_SRC/src/switch_rtp.c"
```

测试应覆盖三种不同结果：合法一位、超时空值、超过最大位数或错误 terminator。只测合法按键无法证明失败路径安全。

## 引导实验

前置条件：DTMF 在 echo 或 milliwatt 之后可听到侧音或测试音。

1. 在源码核对 `read` 参数含义，抄到笔记（不要改 XML）。

2. 若 `demo_ivr` 可用：拨 `5000`，按提示键。缺声音则跳过，标 unavailable。

3. 用教程 context 做受控 originate（不依赖 9000 的 default 入口）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'xml_locate dialplan context name tutorial'
   ```

   若 context 尚不存在（未挂载 tutorial-ivr.xml），本课用纸面分析 `read` 超时路径，实验标 unavailable，第 18 天挂载后再补做。

   若已存在：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'originate {ignore_early_media=true}loopback/9000@tutorial &park'
   ```

   在 5 秒内不按键，观察呼叫结束。再来一次，按 `9`（主菜单未定义）。确认不会执行奇怪 API。最后 `hupall`。

4. 说明：变量 `tutorial_main_digit` 只应是单字符或空。若有人把 `';unload mod_sofia'` 填进 XML data，模块 allowlist 仍应拒绝（第 23 天验证）。

清理：`hupall`。关闭 siptrace。

## 独立挑战

画出 timeout、非法键、合法 `1` 三条路径，标注每条是否调用 `tutorial_ivr_metric`。

## 验收

**验收方式：半自动。**

- **pass**：能解释 `read` 六个字段；超时不等于默认选择。
- **fail**：把空变量当成 `'1'`。
- **unavailable**：tutorial context 未挂载且 demo IVR 无声音。

## 故障排查

- 现象：按键无反应 → 证据：RTP 无 telephone-event → 原因：第 9 天 DTMF 模式 → 恢复：先 echo，再 IVR。
- 现象：一直听到提示 → 证据：timeout 太长或 IVR 重试 → 原因：vanilla demo_ivr 菜单循环 → 恢复：这是 demo 行为，教程 XML 超时后 hangup。
- 现象：输入被当命令 → 证据：CLI 出现奇怪 API → 原因：错误地把 DTMF 拼进 `system` 应用 → 恢复：教程禁止该模式，只用 allowlist 应用。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 收号 | read | 把 DTMF 写入 channel 变量的应用 |
| 超时 | timeout | 未按满位数时结束收集 |
| 终止键 | terminator | 提前结束输入的 DTMF，常用 `#` |
| 非法输入 | invalid input | 格式或菜单不允许的选择 |
