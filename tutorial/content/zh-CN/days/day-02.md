# 第 2 天：Docker、端口、日志与 fs_cli

## 今日成果

- 能区分 Docker 主机路径、容器内安装路径和本地源码路径，避免在错误位置修改配置。
- 能用 `docker ps`、`fs_cli -x status`、Sofia 状态与容器日志形成一条最小健康证据链。
- 能按信令、WebSocket、ESL 和 RTP 的用途解释实验端口，而不是盲目开放全部端口。

## 核心原理

本教程同时存在三个文件系统视角：MacBook 上的源码与教学网站、Ubuntu 主机上的 Docker 管理面、`freeswitch` 容器内的安装树。源码中的 [`conf/vanilla/`](../../../../conf/vanilla/) 是示例输入；运行进程实际读取的是安装前缀下的配置。修改源码文件不会自动改变已启动容器，反过来在容器里临时改文件也不会成为可复现的源码变更。

`fs_cli` 不是直接调用进程函数。它通过 ESL 连接 `mod_event_socket`，发送 API 命令并读取响应。因此“容器 Up”“FreeSWITCH core UP”“ESL 可认证”“Sofia profile RUNNING”是四条不同证据。排障时应从外到内逐层确认。

实验常用端口：5060/5080 是 SIP，5066 是浏览器 WS，7443 是 WSS，8021 是 ESL；RTP 使用 UDP 端口范围。SIP 端口可达不代表 RTP 双向可达，WSS TLS 握手成功也不代表 SIP Digest 注册成功。ESL 8021 应限制在 localhost 或可信管理网络。

## 源码导航

- [`docker/README.md`](../../../../docker/README.md)：FreeSWITCH 容器网络约束。
- [`docker/examples/Debian11/Dockerfile`](../../../../docker/examples/Debian11/Dockerfile)：源码构建镜像所需依赖和安装过程。
- [`docker/base_image/healthcheck.sh`](../../../../docker/base_image/healthcheck.sh)：官方容器以 `fs_cli -x status` 的 `UP` 作为健康依据。
- [`libs/esl/fs_cli.c`](../../../../libs/esl/fs_cli.c)：CLI 客户端入口和参数处理。
- [`src/mod/event_handlers/mod_event_socket/`](../../../../src/mod/event_handlers/mod_event_socket/)：ESL 服务端实现。
- [`conf/vanilla/autoload_configs/event_socket.conf.xml`](../../../../conf/vanilla/autoload_configs/event_socket.conf.xml)：演示 ESL 监听配置；默认口令不是生产安全基线。
- [`man/0.getting-started/02-quick-start.md`](../../../../man/0.getting-started/02-quick-start.md)：本仓库实际验证过的 Docker、安装路径、端口与日志命令。

## 源码深挖

`fs_cli -x` 的本质是 ESL 的同步 API 请求，不是直接调用 FreeSWITCH 内部函数。服务端由 `mod_event_socket` 创建 TCP listener；收到 `api <command>` 后执行 API 接口，再把文本 reply 写回连接。事件连接则先认证，再 `event plain ...` 订阅，后续由 socket 线程持续推送事件。

这解释了 Docker 排障中的两个常见误区：容器里 `fs_cli -x status` 成功，只说明容器到 8021 的路径和认证可用；宿主机访问 8021 失败，可能只是端口没有发布或 ACL 拒绝，并不等于 FreeSWITCH 未启动。日志同理，日志文件是进程输出；端口是 listener；两者属于不同证据层。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "mod_event_socket_runtime|switch_socket_accept|api_command|event plain|SWITCH_MODULE_LOAD_FUNCTION" \
  "$FREESWITCH_SRC/src/mod/event_handlers/mod_event_socket/mod_event_socket.c"
```

阅读 `mod_event_socket_runtime` 时看清三段：bind/listen、accept 新连接、为连接创建 listener。任何一段失败，`docker ps` 仍可能显示容器为 running，所以本课验收必须同时看 health、日志和 `fs_cli`。

## 引导实验

实验拓扑：MacBook 运行教学网站，Ubuntu `10.100.212.8` 运行名为 `freeswitch` 的容器。先登录 Docker 主机：

```bash
ssh walter@10.100.212.8
```

1. 确认容器身份和运行时间：

   ```bash
   docker ps --filter name=freeswitch \
     --format '{{.Names}} {{.Status}} {{.Image}}'
   ```

   当前已验证的实验结果是容器名 `freeswitch`、镜像 `freeswitch:local`，状态为 `Up`。不要把运行时长写成固定断言。

2. 确认 core：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
   ```

   pass 证据是第一行以 `UP` 开头，并包含版本、当前 session、累计 session 和容量。本环境已验证为 FreeSWITCH `1.11.3-dev`，安装前缀是 `/usr/local/freeswitch`。

3. 确认 Sofia：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x "sofia status"
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x "sofia status profile internal"
   ```

   当前 internal profile 为 `RUNNING`，SIP IP 为 `10.100.212.8`，WS/WSS 分别绑定 5066/7443，codec 包含 OPUS，DTMF mode 为 RFC2833。

4. 查看注册，但不暴露认证头：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x "sofia status profile internal reg"
   ```

   当前未启动软电话时返回 0 项是合理状态，不等于 profile 故障。

5. 只有出现异常时才读取有限日志：

   ```bash
   docker logs --tail 100 freeswitch
   ```

   记录时间、模块和错误类别即可。分享前删除 Authorization、号码、地址和完整 UUID。

6. 从 MacBook 做端口可达性检查：

   ```bash
   nc -vz -w 2 10.100.212.8 5060
   nc -vz -w 2 10.100.212.8 7443
   ```

   不要把 TCP 8021 可达当作允许远程 ESL；本环境的 ACL 会拒绝远端 ESL 会话，这是正确的安全边界。

清理：本课不创建容器、不更改防火墙，也不需要停止 FreeSWITCH。若临时开启了额外 trace，退出前关闭。

## 独立挑战

在不查看上文的情况下列出下面四种现象分别应该先查哪一层证据：容器不存在、`fs_cli` 无法认证、Sofia internal 未运行、通话建立但无声音。然后说明为什么 `docker ps` 显示 Up 不能证明 WebRTC 音频正常。

## 验收

**验收方式：自动**

- **pass**：容器为 Up，`fs_cli -x status` 返回 `UP`，internal Sofia profile 为 RUNNING，并能指出容器日志命令和实际安装前缀。
- **fail**：命令可执行，但 core 或必需 profile 明确返回错误；保存脱敏证据后按层排查。
- **unavailable**：SSH、Docker daemon 或 ESL 当前不可达，无法取得可靠状态；不要用上一次结果冒充当前健康。

已在 Ubuntu 22.04.5 LTS Docker 主机 `10.100.212.8` 验证上述 `docker ps`、core status、Sofia profile 与 registration 命令。验证时 registrations 为 0，属于实验尚未注册终端的预期状态。

## 故障排查

- 现象：SSH 可达但 Docker 命令失败 → 证据：权限或 daemon 错误 → 原因：用户不在 docker 组或 daemon 停止 → 恢复：在主机修复 Docker 权限/服务，不在容器内修。
- 现象：容器 Up，`fs_cli` 拒绝 → 证据：认证或 socket 错误 → 原因：ESL ACL、密码、模块或 ready 状态 → 恢复：在容器内使用本地 `fs_cli`，检查有限日志，避免向网络开放 8021。
- 现象：internal profile 缺失 → 证据：`sofia status` 无对应 RUNNING 行 → 原因：模块未加载或 XML/profile 启动失败 → 恢复：查启动日志和运行时模块配置。
- 现象：7443 TLS 握手成功但浏览器 WSS 失败 → 证据：证书不受信任或 SAN 不匹配 → 原因：TLS 身份校验失败 → 恢复：部署与实际域名/IP 匹配且被浏览器信任的证书，不关闭校验。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 安装前缀 | install prefix | 容器内实际二进制、配置、模块和日志的根路径 |
| 事件套接字 | Event Socket | `fs_cli` 和外部程序使用的 ESL 服务端接口 |
| 健康证据 | health evidence | 某一层当前状态的可观察结果，而不是推测 |
| 绑定地址 | bind address | 服务实际监听的本地地址和端口 |
| 访问控制列表 | access control list | 决定哪些来源可连接 ESL/SIP 等接口的规则 |
