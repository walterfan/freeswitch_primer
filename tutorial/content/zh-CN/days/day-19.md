# 第 19 天：ESL 认证、命令与事件

## 今日成果

- 分清 ESL **命令连接**（同步 API）与 **事件连接**（订阅后持续推送）。
- 只用 allowlist：`status`、`sofia status`、`tutorial_metrics`。
- 证明教学网站的 public-config 永不包含 ESL 密码。

## 核心原理

`mod_event_socket` 默认监听 8021，演示口令在 [`event_socket.conf.xml`](../../../../conf/vanilla/autoload_configs/event_socket.conf.xml)。本 lab 的 ACL 拒绝从 MacBook 直连 8021，这是正确边界：应在容器内跑 `fs_cli`，或让教学服务在 Docker 主机网络内连接。

教学 C++ 客户端：命令走 `EslCommandClient`，事件走独立 reader。allowlist 在 [`esl_observer.cpp`](../../../site/src/esl_observer.cpp)。认证失败必须是 degraded/unavailable，不能把 password 写进 JSON 或页面。

事件里的 `Unique-ID` 只用于服务端关联；浏览器看到的应是脱敏后的相关标识。

## 源码导航

- [`libs/esl/src/esl.c`](../../../../libs/esl/src/esl.c)
- [`tutorial/site/src/esl_client.cpp`](../../../site/src/esl_client.cpp)
- [`tutorial/site/src/esl_observer.cpp`](../../../site/src/esl_observer.cpp)
- [`conf/vanilla/autoload_configs/event_socket.conf.xml`](../../../../conf/vanilla/autoload_configs/event_socket.conf.xml)
- [本课实验目录](../../../labs/day-19/)

## 源码深挖

ESL 命令连接和事件连接共享认证/传输基础，但执行模型不同。`libs/esl/src/esl.c:esl_connect_timeout` 建立连接，`esl_send_recv_timed` 发送命令并等待 reply；事件连接则由 `esl_recv_event_timed` 持续读取 framed event。服务端 `mod_event_socket` 把 API reply 与 event subscription 分开处理。

事件 socket 是一个可远程控制面，密码、ACL、监听地址和 allowlist 都是安全边界。教程客户端只允许固定命令名，并把原始事件映射成脱敏结构；不要把浏览器输入直接拼入 `api` 或 `bgapi`。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "esl_connect_timeout|esl_send_recv_timed|esl_recv_event_timed|event_socket_auth|api_command|SWITCH_MODULE_RUNTIME_FUNCTION" \
  "$FREESWITCH_SRC/libs/esl/src/esl.c" \
  "$FREESWITCH_SRC/src/mod/event_handlers/mod_event_socket/mod_event_socket.c"
```

认证失败、命令超时、事件断连应分别呈现为 unavailable/degraded；不要因为 API 连接成功就假设事件 reader 也在工作。

## 引导实验

前置条件：容器内 `fs_cli -x status` 已成功（它本身就是 ESL）。

1. 证明远端直连应失败（从 MacBook，预期 timeout 或拒绝）：

   ```bash
   nc -vz -w 2 10.100.212.8 8021 || true
   ```

   不要把成功的远程 8021 当成教程目标。

2. 容器内执行 allowlist 等价命令：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'sofia status'
   ```

3. 看网站配置契约：

   ```bash
   curl -fsS http://127.0.0.1:7009/api/v1/public-config
   grep -n 'password_env' tutorial/site/config/localhost-https.yaml
   ```

   JSON 不得出现口令。密码只通过 `FS_TUTORIAL_ESL_PASSWORD` / `TUTORIAL_ESL_PASSWORD` 注入。

4. 若在隔离 Docker 主机启用 ESL（`esl.enabled: true` 且服务与 FS 同网络）：

   ```bash
   curl -fsS http://127.0.0.1:7009/api/v1/health
   ```

   观察 `esl` / `heartbeat` 从 unavailable 变为 healthy。打一通短呼叫，打开 `/api/v1/events` 或页面 Event 面板，确认能看到 CHANNEL 类事件且无完整 UUID/号码。

清理：不要为了方便而注释掉 inbound ACL。不要把 `ClueCon` 写进截图说明。

## 独立挑战

设计认证失败演练：用户可见的三个字段（组件名、degraded、remediation），明确不可见的三个字段（password、Authorization、完整 UUID）。

## 验收

**验收方式：半自动。**

- **pass**：public-config 无密码；命令 allowlist 可复述；远程 8021 保持不可用或仅限可信管理网。
- **fail**：页面可输入任意 ESL 命令。
- **unavailable**：教学服务未启用 ESL。

## 故障排查

- 现象：auth rejected → 证据：health.esl degraded → 原因：环境变量与 event_socket 口令不一致 → 恢复：只改注入变量。
- 现象：reply 被 event 打断 → 证据：命令连接读到 HEARTBEAT → 原因：单 socket 混用 → 恢复：保持双连接设计。
- 现象：事件泄漏 UUID → 证据：浏览器 SSE 原文 → 原因：normalize 未替换 → 恢复：检查 EventHub 脱敏。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 事件套接字 | ESL | 命令与事件的 TCP 接口 |
| 允许列表 | allowlist | 教学服务可执行的只读命令集合 |
| 订阅 | subscription | 事件连接上请求的事件类 |
| 降级 | degraded | 认证或依赖失败后的诚实状态 |
