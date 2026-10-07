# 第 4 天：XML 配置域与变量时机

## 今日成果

- 能说清 FreeSWITCH XML 的 configuration、directory、dialplan 三个主要 section 分别回答什么问题。
- 能区分预处理变量 `$${name}` 与呼叫时 channel 变量 `${name}`，并用只读 API 验证实际 domain、用户与 route。
- 能安全执行 `reloadxml` 的概念演练，同时明确不能编辑运行中的 `freeswitch.xml.fsxml`。

## 核心原理

FreeSWITCH 启动或 `reloadxml` 时先把入口 XML、`X-PRE-PROCESS` 指令和 include 展开成一棵运行时 XML 树。configuration section 配置模块；directory section 提供 domain、user、认证参数与用户变量；dialplan section 按 context 和 extension 条件为新 session 生成 application 执行序列。

变量替换时机决定了值从哪里来：`$${domain}` 是预处理变量，在 XML 构建阶段展开；`${destination_number}` 是 channel 变量，在具体呼叫执行时读取。把二者混用会导致所有呼叫共享错误的静态值，或让本应在预处理时确定的路径留成无法解析的字符串。

vanilla 中 `vars.xml` 把 `domain` 设为 `$${local_ip_v4}`，再令 `domain_name=$${domain}`。directory 的用户 1000 属于该 domain；dialplan 的 `default` context 中，`Local_Extension` 使用呼叫时的 `${destination_number}` 和 `${domain_name}` 查找并 bridge 用户。三者形成“预处理 domain → directory 用户 → 呼叫时 dialplan”链路。

## 源码导航

- [`conf/vanilla/freeswitch.xml`](../../../../conf/vanilla/freeswitch.xml)：演示配置入口及 section include 顺序。
- [`conf/vanilla/vars.xml`](../../../../conf/vanilla/vars.xml)：`X-PRE-PROCESS` 变量定义；注释不能禁用一条预处理指令，必须删除该行。
- [`conf/vanilla/directory/default/1000.xml`](../../../../conf/vanilla/directory/default/1000.xml)：演示用户 1000；默认密码只可用于隔离实验。
- [`conf/vanilla/dialplan/default.xml`](../../../../conf/vanilla/dialplan/default.xml)：`default` context 与 `Local_Extension` 路由。
- [`src/switch_xml.c`](../../../../src/switch_xml.c)：XML 解析、预处理、定位与 reload 的 core 实现。
- [`src/mod/dialplans/mod_dialplan_xml/mod_dialplan_xml.c`](../../../../src/mod/dialplans/mod_dialplan_xml/mod_dialplan_xml.c)：把 channel 条件与 XML extension 匹配成 application 序列。
- [`src/mod/applications/mod_commands/mod_commands.c`](../../../../src/mod/applications/mod_commands/mod_commands.c)：`global_getvar`、`user_exists`、`xml_locate` 与 `reloadxml` 等 API 入口。

## 源码深挖

XML 配置会先进入全局 XML 树，再由具体消费者查找节点。`switch_xml_open_root`/`__switch_xml_open_root` 负责装载和重载根树；dialplan 不是 core 自动“猜”出来的，而是 `switch_core_session` 根据 caller profile 选择 dialplan interface，再调用对应的 `hunt_function`。这就是 `context`、`dialplan` 和 `destination_number` 必须分别验证的原因。

变量也有生命周期：`${}` 在 dialplan 执行时从 channel/profile 展开，`$${}` 是配置变量展开，环境变量或 XML 中的值不会自动变成每个 session 的动态状态。把外部 SIP header 直接拼入文件路径、命令或 XML 查询，都会把不可信输入带进更高权限的配置面。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_xml_open_root|__switch_xml_open_root|hunt_function|destination_number|switch_channel_get_variable" \
  "$FREESWITCH_SRC/src/switch_xml.c" "$FREESWITCH_SRC/src/switch_core_session.c" \
  "$FREESWITCH_SRC/src/mod/dialplans/mod_dialplan_xml/mod_dialplan_xml.c"
```

读一条拨号计划时，沿着“XML condition → extension → action → application interface”走，不要把 XML 文件名当成执行入口。`reloadxml` 只重建 XML 树；模块、Sofia profile 和正在运行的 session 不会因此全部重启。

## 引导实验

前置条件：`freeswitch` 实验容器运行，安装前缀为 `/usr/local/freeswitch`。本课只读取运行时 XML，不修改配置。

1. 从源码确认预处理变量链：

   ```bash
   grep -n 'data="domain=\|data="domain_name=' conf/vanilla/vars.xml
   ```

   预期看到 `domain=$${local_ip_v4}` 和 `domain_name=$${domain}`。这里的双美元符号表示 XML 预处理时机。

2. 从运行进程读取展开后的全局值：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'global_getvar domain'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'global_getvar domain_name'
   ```

   当前实验环境两者均为 `10.100.212.8`。不要把这个 IP 写入可复用的生产配置；它只是当前 lab 的展开结果。

3. 安全确认用户 1000 可由 directory 解析：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'user_exists id 1000 10.100.212.8'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'user_data 1000@10.100.212.8 var user_context'
   ```

   第一条应返回 `true`，第二条通常返回 `default`。不要把 `find_user_xml` 的完整输出粘贴到报告中，因为它可能包含演示 SIP 密码和其他认证参数。

4. 只定位 default context 中的 route 名称：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'xml_locate dialplan context name default' \
     | grep -n -m1 'extension name="Local_Extension"'
   ```

   命中说明运行时 XML 树中存在本地分机 route。它尚未证明一次具体 INVITE 的条件已匹配；第 8 天会结合 channel 变量和日志跟踪 dialplan hunt。

5. 在源码中比较变量时机：

   ```bash
   grep -n 'Local_Extension' conf/vanilla/dialplan/default.xml
   sed -n '247,272p' conf/vanilla/dialplan/default.xml
   ```

   观察 `${destination_number}`、`${dialed_extension}` 等单美元 channel 变量，以及引用已预处理全局设置的 `${domain_name}` channel 值。

`reloadxml` 会重新构建 XML 树，但本课不需要执行。后续修改教程专用挂载文件时，先校验 XML，再运行 `fs_cli -x reloadxml`，并检查返回 `+OK [Success]`。绝不直接编辑 `log_dir` 下内存映射的 `freeswitch.xml.fsxml`。

清理：本课没有运行时变更。若自行在隔离环境做 reload，确认日志无 XML error；不要重启或覆盖共享实验配置来掩盖解析失败。

## 独立挑战

从 directory 用户 1001 出发，给出“预处理 domain 值 → user 存在 → user_context → default/Local_Extension route”四步只读证据，并标注每一步发生在预处理、运行时全局查询、directory lookup 还是呼叫时 dialplan。答案不得包含密码或完整用户 XML。

## 验收

**验收方式：半自动**

- **pass**：`domain` 与 `domain_name` 可读取，`user_exists id 1000 <domain>` 返回 `true`，运行时 default context 可定位 `Local_Extension`，并能正确解释 `$${name}` 与 `${name}` 的时机差异。
- **fail**：XML API 可执行，但 domain、用户或 route 明确缺失；检查预处理/include 顺序及 XML 错误，不直接改 compiled fsxml。
- **unavailable**：FreeSWITCH、`fs_cli` 或运行时 XML API 不可达，无法验证当前配置；源码文件存在不能替代运行时证据。

已在 Ubuntu 22.04.5 LTS 的远端实验容器验证 `domain=domain_name=10.100.212.8`、用户 1000 存在、`user_context=default`，并从 runtime dialplan 定位到 `Local_Extension`。自动证据验证结构，具体呼叫匹配留到第 5/8 天。

## 故障排查

- 现象：`global_getvar domain` 为空 → 证据：预处理全局变量缺失 → 原因：`vars.xml` 未 include、预处理指令错误或运行的不是预期配置目录 → 恢复：确认 `conf_dir` 和入口 XML，校验后 reload。
- 现象：用户 XML 文件存在但 `user_exists` 为 `false` → 证据：源码/磁盘与 runtime XML 不一致 → 原因：domain 不匹配、directory include 未展开或 reload 失败 → 恢复：使用实际 global domain，检查运行时 directory 定位与错误日志。
- 现象：用户存在但拨号不进入本地 route → 证据：directory lookup 正常、dialplan hunt 未命中 → 原因：profile context、destination_number 或正则条件不匹配 → 恢复：先比较 channel context 和 route 条件，不修改用户认证。
- 现象：reload 后大量配置异常 → 证据：XML parse/preprocess error → 原因：include、实体、变量或标签错误 → 恢复：回滚刚改的教程专用文件并重新校验，不能手工修补 `freeswitch.xml.fsxml`。

临时开启 XML/dialplan 调试后必须恢复原日志级别；分享输出时删除密码、Authorization、地址、号码和完整 UUID。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| XML 预处理 | XML preprocessing | include 和 `$${name}` 在运行时 XML 树建立前展开的阶段 |
| 目录 | directory | domain、user、认证参数与用户变量的查询空间 |
| 拨号计划 | dialplan | 按 context、extension 和 condition 生成 application 执行序列的规则 |
| 上下文 | context | 隔离 dialplan 路由命名空间的运行时选择 |
| 通道变量 | channel variable | 一条呼叫腿执行期间以 `${name}` 读取的动态值 |
| 编译 XML | compiled XML | FreeSWITCH 预处理后的运行时 fsxml；不是供直接编辑的源配置 |
