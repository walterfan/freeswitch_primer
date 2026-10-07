# 第 8 天：INVITE 路由、context 与 bridge

## 今日成果

- 用 `xml_locate` 和一次真实 INVITE 证明 `Local_Extension` 命中。
- 能解释 originate/bridge 创建 B-leg，以及被叫未注册时的 hangup cause。
- 能区分预处理 `$${domain}` 与呼叫时 `${destination_number}`。

## 核心原理

已认证 INVITE 的 channel 使用 directory 的 `user_context`（vanilla 为 `default`）。`mod_dialplan_xml` 按 context → extension → condition 生成 application 列表。`Local_Extension` 的正则是 `^(10[01][0-9])$`，因此 1000–1019 命中，9000/5000 走别的 extension。

`bridge user/1001@${domain_name}` 在 [`switch_ivr_originate.c`](../../../../src/switch_ivr_originate.c) 里 originate B-leg。被叫未注册时典型 cause 是 `USER_NOT_REGISTERED` 或 `UNALLOCATED_NUMBER`，不是 `NO_ROUTE_DESTINATION`（后者表示 dialplan 没命中）。`continue_on_fail=true` 之后还会尝试 voicemail loopback，所以“失败”不一定立刻 hangup。

internal profile 的 XML `context=public` 只作用于**未认证**入站；不要用它解释 1000 拨 1001。

## 源码导航

- [`conf/vanilla/dialplan/default.xml`](../../../../conf/vanilla/dialplan/default.xml) 约 247–272 行：`Local_Extension`。
- [`src/mod/dialplans/mod_dialplan_xml/mod_dialplan_xml.c`](../../../../src/mod/dialplans/mod_dialplan_xml/mod_dialplan_xml.c)
- [`src/switch_ivr_originate.c`](../../../../src/switch_ivr_originate.c)
- [`src/switch_core_state_machine.c`](../../../../src/switch_core_state_machine.c)
- [本课实验目录](../../../labs/day-08/)

## 源码深挖

收到 INVITE 后，Sofia 把目标号码和 caller profile 交给 core；core 根据 profile 的 `dialplan` 找到 dialplan interface，再调用 `hunt_function`。XML dialplan 返回 extension 后，session 按顺序执行 answer、playback、bridge 等 application。`switch_ivr_originate` 负责创建/等待 B-leg，`switch_ivr_bridge` 才负责把两条已建立的 session 接起来。

这条链上有两个容易混淆的时机：`answer` 是 A-leg 的 channel 状态，`bridge` 是两腿的媒体/控制关系；一个 extension 可以在 B-leg 接通前播放 ringback，也可以在接通后执行 bridge 后动作。`show channels` 看到两腿不等于媒体已经双向流动。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "hunt_function|switch_ivr_originate|switch_ivr_bridge|bridge_function|SWITCH_ADD_APP" \
  "$FREESWITCH_SRC/src/switch_core_session.c" \
  "$FREESWITCH_SRC/src/switch_ivr_originate.c" \
  "$FREESWITCH_SRC/src/switch_ivr_bridge.c" \
  "$FREESWITCH_SRC/src/mod/applications/mod_dptools/mod_dptools.c"
```

故障定位从上游到下游：INVITE 是否进入正确 profile → dialplan 是否返回 extension → originate 是否创建 B-leg → bridge 是否执行 → 两腿各自的 RTP 是否有包。

## 引导实验

前置条件：1000 已注册。1001 先保持注册，再故意注销做对比。

1. 定位运行时 route 名称：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'xml_locate dialplan context name default' \
     | grep -n -m1 'extension name="Local_Extension"'
   ```

   在源码核对正则与 bridge：

   ```bash
   sed -n '247,272p' conf/vanilla/dialplan/default.xml
   ```

2. 1000 拨 `1001`。接通前执行：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels'
   ```

   确认 dest 为 1001，application 最终为 bridge。挂断。

3. 注销 1001，1000 再拨 1001。观察是否进入 voicemail 或迅速结束。用只读 API 看最后一通（不要分享完整 UUID）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show calls count'
   ```

   在实验笔记写 hangup 类别：未注册 / 无路由 / 正常挂断。需要 cause 时在通话刚结束时用：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'console loglevel notice'
   ```

   从刚结束的日志中只抄 `Hangup` 行的 cause 名，删除 UUID。

4. 拨一个不存在的目标 `1099`（仍可能命中 `10[01][0-9]`）对比拨 `8888`：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'originate {ignore_early_media=true}loopback/8888/default &park'
   ```

   预期迅速失败。`loopback/8888/default` 在 default context 寻找 8888；vanilla 通常无此 extension。记录 cause 为无路由类，然后：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'hupall'
   ```

清理：`hupall` 只用于本课 originate 残留。重新注册 1001。

## 独立挑战

把 INVITE 画成五个节点：Request-URI → 认证后 context → condition 字段 `${destination_number}` → `export dialed_extension` → `bridge user/...@${domain_name}`。标注 `${}` 与 `$${ }` 各自展开时机。

## 验收

**验收方式：半自动。**

- **pass**：能指出 `Local_Extension` 正则；能区分未注册与无路由；没有把 profile 的 public context 当成已认证呼叫的 context。
- **fail**：8888 与 1001 被说成同一条 route。
- **unavailable**：无法 originate 或无法定位 XML。

## 故障排查

- 现象：1001 在线仍无匹配 → 证据：destination 含前缀或 context 为 public → 原因：UA 拨号规则、未认证 INVITE → 恢复：检查号码与认证。
- 现象：bridge 失败进语音信箱 → 证据：`continue_on_fail=true` → 原因：被叫未注册仍继续执行后续 action → 恢复：这是 vanilla 行为，不是 core 崩溃。
- 现象：把 `global_getvar domain` 当 channel 变量改 → 证据：所有呼叫 domain 被写死 → 原因：混用 `$${}` / `${}` → 恢复：第 4 天变量时机。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 拨号计划狩猎 | dialplan hunt | 按 context/extension/condition 选择 action |
| 发起 | originate | 为 B-leg 创建新 session |
| 桥接 | bridge | 把 A-leg 与 B-leg 的信令/媒体连起来 |
| 挂断原因 | hangup cause | session 结束分类，用于区分无路由与未注册 |
