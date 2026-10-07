# 第 1 天：FreeSWITCH 系统模型与健康检查

## 今日成果

- 能用“信令、媒体、控制、教学服务”四个平面解释一次实验通话，而不把网站当作媒体代理。
- 能确认 FreeSWITCH 进程处于 `UP`，并从教学服务健康接口区分“服务可用”和“实验依赖可用”。
- 能指出进程入口、核心初始化、SIP endpoint、ESL 和教程代码各自所在的位置。

## 核心原理

FreeSWITCH 是一个模块化 softswitch。先把系统拆成四个角色，后面遇到注册失败、无声音或指标陈旧时，才能在正确的边界找证据。

1. **信令平面**：SIP 用户代理通过 `mod_sofia` 完成 REGISTER、INVITE、应答和 BYE。第 11–15 天，浏览器也走 SIP over WSS；它不是通过教学网站转发 SIP。
2. **媒体平面**：FreeSWITCH 协商 SDP，终止 RTP 或 WebRTC 的 ICE、DTLS-SRTP，并按需要桥接或转码音频。网页只把浏览器的远端音轨连接到 `<audio>`。
3. **控制与观测平面**：`fs_cli` 和本教程的 C++ 服务通过 ESL 连接 `mod_event_socket`。它们执行有限的只读命令、订阅事件和生成 Metrics，不承载媒体。
4. **教学平面**：Crow 服务提供 Markdown、健康状态和声明式实验检查。即使 FreeSWITCH 或 ESL 不可用，课程正文也应继续可读。

一次浏览器到 SIP 软电话的目标链路是：浏览器 SIP.js → WSS → Sofia internal profile → FreeSWITCH session/media core → Sofia → SIP 软电话。教学服务位于链路旁边观察，而不在音频路径中。

## 源码导航

- [`src/switch.c`](../../../../src/switch.c)：`main` 和进程参数入口；启动路径调用 `switch_core_init_and_modload`。
- [`src/switch_core.c`](../../../../src/switch_core.c)：核心运行时和 HEARTBEAT 事件；事件包含当前 session 数、容量和 uptime 等后续 Metrics 原始证据。
- [`src/switch_core_session.c`](../../../../src/switch_core_session.c)：session 生命周期。每条通话腿都是独立 session，并拥有 UUID。
- [`src/mod/endpoints/mod_sofia/mod_sofia.c`](../../../../src/mod/endpoints/mod_sofia/mod_sofia.c)：SIP endpoint 模块入口。
- [`src/mod/event_handlers/mod_event_socket/`](../../../../src/mod/event_handlers/mod_event_socket/)：ESL 服务端模块；[`libs/esl/`](../../../../libs/esl/) 包含 `fs_cli` 使用的客户端库。
- [`tutorial/site/src/main.cpp`](../../../site/src/main.cpp)：教学服务入口，只注册预定义检查并启动 HTTP 服务。
- [`man/0.getting-started/02-quick-start.md`](../../../../man/0.getting-started/02-quick-start.md) 与 [`man/1.architecture/03-repo-map.md`](../../../../man/1.architecture/03-repo-map.md)：本仓库已经验证的环境命令和源码地图。

今天不要求读懂这些文件。先建立“进程入口 → core → loadable module → XML/runtime → ESL observer”的导航顺序。

## 源码深挖

FreeSWITCH 的“启动成功”不是某个端口打开这么简单，而是 core、模块和事件系统按顺序就绪：`src/switch.c:main` 解析启动参数，随后调用 `switch_core_init_and_modload`；后者先初始化 core，再调用 `switch_loadable_module_init`，最后发布 `SWITCH_EVENT_STARTUP`。因此启动日志里出现 `FreeSWITCH Started` 只能证明进入运行态，不能证明 Sofia profile 或 ESL 已经可用。

会话也不是一个简单的 socket。创建入口在 `switch_core_session_request*`，拿到 endpoint 的 session 后由 `switch_core_session_thread` 运行状态机，线程退出时再移除 media bug、标记 destroyed 并释放 session。排障时应先确认 core/session，再确认 endpoint/module，最后确认媒体。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_core_init_and_modload|SWITCH_EVENT_STARTUP|switch_core_session_thread|switch_core_session_request" \
  "$FREESWITCH_SRC/src/switch.c" "$FREESWITCH_SRC/src/switch_core.c" \
  "$FREESWITCH_SRC/src/switch_core_session.c"
```

读源码时重点追三条证据：`status` 证明 core 可答命令；`show modules`/`module_exists` 证明动态模块已注册；`show channels` 证明 session 已进入通话生命周期。不要用其中一条替代另外两条。

## 引导实验

前置条件：在 Ubuntu/Debian 主机上已按 `man/0.getting-started/02-quick-start.md` 启动名为 `freeswitch` 的实验容器。命令假定容器内安装前缀是 `/usr/local/freeswitch`；如果你的环境不同，以实际 `fs_cli` 路径替换。

1. 确认容器存在并保持运行：

   ```bash
   docker ps --filter name=freeswitch
   ```

   预期 `STATUS` 以 `Up` 开头。若容器不存在，本课状态是 **unavailable**，不是课程失败。

2. 读取 FreeSWITCH core 状态：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
   ```

   预期输出第一行包含 `UP`，并显示 session、session rate 和 uptime。今天只确认字段存在，不把某个具体数值写成固定答案。

3. 确认 SIP endpoint profile：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x "sofia status"
   ```

   vanilla 实验通常显示 `internal` 与 `external`。Profile 缺失属于 SIP 配置或模块问题；它不表示 core 一定停止。

4. 启动教学网站后读取服务健康：

   ```bash
   curl -fsS http://127.0.0.1:7009/api/v1/health
   ```

   HTTP 200 证明 Crow 服务可达。JSON 中 `service` 和 `content` 可为 `healthy`，而尚未连接的 `freeswitch`、`esl`、`heartbeat`、`sip_profile`、`metrics` 应明确显示 `unavailable`，不能伪造为零或健康。

5. 打开 `http://127.0.0.1:7009/#day-01`，在左侧课程地图确认 30 天、6 个阶段均可导航。右侧实验状态应把不可用组件及恢复建议分开显示。

清理：本实验只读，无需停止容器。不要开启 SIP trace；不要把健康输出当作公开监控端点暴露到互联网。

## 独立挑战

画出“浏览器呼叫 1000”和“网页点击健康检查”两条路径。每条路径至少标出发起者、协议、FreeSWITCH 模块或教学服务，以及结果返回方向。然后回答：如果网页能打开但 `sofia status` 失败，四个平面中哪个仍然健康，哪个需要调查？

合格答案必须明确：浏览器的 SIP/WSS 和音频不经过 Crow；健康检查只提供控制/观测证据；网页正文可在 FreeSWITCH 不可用时继续阅读。

## 验收

**验收方式：自动**

- **pass**：`POST /api/v1/checks/day-01/service-health` 返回 `pass`，且证据显示所有当前必需组件健康。
- **fail**：教学服务可执行检查，但健康文档结构、课程 manifest 或静态内容不符合契约。按返回的 remediation 检查站点配置和内容校验。
- **unavailable**：教学服务运行，但 FreeSWITCH/ESL 观察尚未启动或依赖不可达。打开 `/api/v1/health` 找到具体组件，再按本课 Docker 和 `fs_cli` 命令恢复。

人工自检：能在不看答案的情况下解释四个平面，并指出无声音首先不能只查 Crow 服务。自动检查不代替后续的双向听感验收。

## 故障排查

- 现象：`docker ps` 没有目标容器 → 证据：列表为空 → 原因：实验环境未启动或容器名不同 → 恢复：按 `man/0.getting-started/02-quick-start.md` 启动，或在后续脚本中配置实际名称。
- 现象：容器为 `Up`，但 `fs_cli -x status` 失败 → 证据：ESL 连接拒绝或认证失败 → 原因：FreeSWITCH 尚未 ready、ESL 未加载、地址或密码不匹配 → 恢复：先看 `docker logs --tail 100 freeswitch`，不要把 ESL 8021 暴露到不可信网络。
- 现象：`status` 为 `UP`，`sofia status` 不见 internal → 证据：core 正常但 SIP profile 缺失 → 原因：`mod_sofia` 未加载或 profile XML 失败 → 恢复：查看启动日志和模块/profile 状态；不要混淆编译清单 `modules.conf` 与运行时 `modules.conf.xml`。
- 现象：网站返回连接失败 → 证据：`curl` 无法连接 127.0.0.1:7009 → 原因：教学服务未启动或配置端口不同 → 恢复：启动站点并保持默认 loopback bind。远程访问必须先配置可信 HTTPS/WSS。

排障输出可能包含地址、号码、UUID 或认证信息；分享前脱敏。临时启用的 trace 必须在收集证据后关闭。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 软交换 | softswitch | 用软件实现呼叫控制、媒体处理和互联的核心进程 |
| 信令平面 | signaling plane | SIP 注册与呼叫状态交换的路径 |
| 媒体平面 | media plane | RTP/WebRTC 音频协商、处理与传输路径 |
| 控制平面 | control plane | ESL 命令、事件与外部运维工具的边界 |
| 会话 | session | FreeSWITCH 中一条独立通话腿及其生命周期 |
| 端点模块 | endpoint module | 把 SIP、Verto 等协议接入 core 的可加载模块 |
| 不可用 | unavailable | 当前不能取得可靠证据；不能伪装成成功或零值 |
