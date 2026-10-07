# 第 18 天：多级 XML IVR 与 channel variables

## 今日成果

- 把 `tutorial-ivr.xml` 挂进运行时 dialplan，形成独立 `tutorial` context。
- 走通主菜单 `1→子菜单 1/2` 以及主菜单 `2/3`，每次安全 hangup。
- 用教程专用 transfer 让 default 里的 9000 进入 tutorial，而不改 `conf/vanilla`。

## 核心原理

文件挂载到 `conf/dialplan/tutorial-ivr.xml` 后，会被 `freeswitch.xml` 的 `dialplan/*.xml` include 成名为 `tutorial` 的 context。认证用户默认仍在 `default`，直接拨 9000 **不会**命中该 context。集成测试使用 `loopback/9000@tutorial`。SIP 话机需要 [`labs/day-18/9000-transfer.xml`](../../../labs/day-18/9000-transfer.xml) 挂到 `conf/dialplan/default/`（教程 overlay，禁止拷进产品 vanilla）。

嵌套 condition 读取 `${tutorial_main_digit}`。`break="never"` 让多个 choice 都能检查。成功路径调用 `tutorial_ivr_metric`，失败路径不调用。

## 源码导航

- [`tutorial/module/mod_tutorial/tutorial-ivr.xml`](../../../module/mod_tutorial/tutorial-ivr.xml)
- [`tutorial/labs/day-18/9000-transfer.xml`](../../../labs/day-18/9000-transfer.xml)
- [`tutorial/deploy/compose.yaml`](../../../deploy/compose.yaml)：IVR XML 只读挂载
- [`src/switch_xml.c`](../../../../src/switch_xml.c)
- [本课实验目录](../../../labs/day-18/)

## 源码深挖

XML IVR 的执行不是 XML 解释器直接递归调用任意函数。XML dialplan 先把 `read`、nested condition 和 `tutorial_ivr_metric` 作为 application 排入当前 channel 的执行流；每次 condition 展开 channel variable，再按 `break` 规则决定是否继续。FreeSWITCH 自带菜单 API 的另一条路径在 `switch_ivr_menu_xml_stack_build`、`switch_ivr_menu_execute`，通过有限 action map 构造菜单栈。

因此 context 是路由命名空间，channel variable 是一次呼叫的状态，module application 是执行能力；三者不可混为一个 XML 文件。教程用 transfer/loopback 进入 `tutorial`，避免把实验规则直接污染 vanilla `default`。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "hunt_function|switch_ivr_menu_xml_stack_build|switch_ivr_menu_execute|break=|switch_channel_get_variable" \
  "$FREESWITCH_SRC/src/switch_core_session.c" "$FREESWITCH_SRC/src/switch_ivr_menu.c" \
  "$FREESWITCH_SRC/src/mod/dialplans/mod_dialplan_xml/mod_dialplan_xml.c" \
  tutorial/module/mod_tutorial/tutorial-ivr.xml
```

菜单实验的最小证据是：进入了哪个 context、收到了哪个变量、命中了哪个 condition、应用返回什么。只看最终 hangup cause 不足以解释中间分支。

## 引导实验

前置条件：能 `reloadxml`。`mod_tutorial` 可延到第 21 天再 load；没有模块时 `tutorial_ivr_metric` 会失败，但 `read`/tone 仍可验证菜单。

1. 确认文件出现在容器 dialplan 目录（Compose overlay 或手工 `docker cp`，不要改 vanilla git 文件）：

   ```bash
   docker exec freeswitch ls -l /usr/local/freeswitch/conf/dialplan/tutorial-ivr.xml
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x reloadxml
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'xml_locate dialplan context name tutorial' | grep -m1 9000
   ```

2. 无 SIP UA 时用 loopback：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'originate {ignore_early_media=true}loopback/9000@tutorial &park'
   ```

   听到第一段 tone 后发送 DTMF（软电话/浏览器）`1` 再 `1`。挂断。再测 `1`+`2`、`2`、`3`。对照 [`browser-to-sip-acceptance.md`](../../../tests/browser-to-sip-acceptance.md) 矩阵。

3. 若希望 1000 直接拨 9000：把 `9000-transfer.xml` 挂到容器 `conf/dialplan/default/90_tutorial_9000.xml`，再 `reloadxml`。用 `xml_locate` 确认 `tutorial-ivr-entry`。实验结束删除该挂载并 reload。

4. 通话中（模块已加载时）查看变量（短 UUID）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'uuid_getvar <UUID> tutorial_ivr_menu'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'uuid_getvar <UUID> tutorial_ivr_choice'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'uuid_getvar <UUID> tutorial_ivr_recorded'
   ```

清理：去掉 transfer 挂载；`hupall`。不要把 XML 提交进 `conf/vanilla`。

## 独立挑战

画出 timeout、invalid、主 1、子 1/2、主 2/3 路径，标注哪些调用教学应用。

## 验收

**验收方式：半自动加人工。**

- **pass**：tutorial context 可定位；至少两条菜单路径有 tone 听感；未修改 vanilla git 树。
- **fail**：reloadxml 失败后去编辑 `freeswitch.xml.fsxml`。
- **unavailable**：无权挂载文件。

## 故障排查

- 现象：拨 9000 进错业务 → 证据：context 仍是 default 且无 transfer → 原因：只挂了 tutorial context → 恢复：loopback@tutorial 或 transfer 文件。
- 现象：reloadxml 失败 → 证据：CLI 非 `+OK` → 原因：XML 损坏 → 恢复：回滚挂载，禁止改 compiled fsxml。
- 现象：子菜单读到旧数字 → 证据：变量未清空 → 原因：未在应用入口清理 → 恢复：第 23 天模块行为。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 多级 IVR | multi-level IVR | 主菜单与子菜单链式收集 |
| 嵌套条件 | nested condition | 上级匹配后继续判断 |
| 上下文转移 | transfer | 把呼叫转到另一 XML context |
| 教程挂载 | tutorial overlay | 只作用于实验容器的额外 XML |
