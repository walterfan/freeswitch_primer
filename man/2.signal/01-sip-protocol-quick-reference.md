# 16. SIP / Sofia-SIP 协议速查手册

<!-- maintained-by: human+ai -->

本章根据 [Sofia-SIP Protocol Conformance](https://sofia-sip.sourceforge.net/refdocs/sofia_sip_conformance.html)
整理，面向 FreeSWITCH `mod_sofia` 的 SIP 报文阅读、功能开发和故障排查。

```{admonition} 使用边界
:class: note

该页面是 Sofia-SIP 1.12.11 的实现速查表，不是 RFC 的替代品。RFC 3261
已经被多个后续 RFC 更新；遇到互通性或合规性问题，应以当前 RFC 和对端设备
的实现为准。
```

## 1. SIP 总览

```text
REGISTER  -> 注册当前联系地址
OPTIONS   -> 能力探测
INVITE    -> 建立或修改会话
ACK       -> 确认 INVITE 的最终响应
CANCEL    -> 取消尚未完成的 INVITE
BYE       -> 结束已建立的会话

SUBSCRIBE -> 订阅事件
NOTIFY    -> 发送事件通知
PUBLISH   -> 发布事件或状态
MESSAGE   -> 发送即时消息
INFO      -> 在对话内发送应用信息
UPDATE    -> 更新早期或已建立会话的 SDP
REFER     -> 转接、转移或请求对端执行动作
```

Sofia-SIP 原生支持 `REGISTER`、`OPTIONS`、`INVITE`、`ACK`、`CANCEL`、
`BYE`，以及 `INFO`、`PRACK`、`SUBSCRIBE`、`NOTIFY`、`UPDATE`、`MESSAGE`、
`REFER`、`PUBLISH` 等扩展方法。

## 2. 三条核心时序

### 2.1 注册与 Digest 认证

```text
UA                         Registrar
 | -------- REGISTER -------> |
 | <--------- 401 ------------ |  WWW-Authenticate
 | -------- REGISTER -------> |  Authorization
 | <--------- 200 ------------ |
```

代理认证通常使用 `407 Proxy Authentication Required`、
`Proxy-Authenticate` 和 `Proxy-Authorization`。

### 2.2 呼叫建立与释放

```text
Caller                         Callee
  | ------ INVITE + SDP --------> |
  | <--------- 100 Trying --------|
  | <--------- 180 Ringing -------|
  | <--------- 200 OK + SDP ------|
  | ------------ ACK ------------>|
  | <========= RTP/RTCP =========>|
  | ------------ BYE ------------>|
  | <----------- 200 OK -----------|
```

`183 Session Progress` 可能携带早期媒体。收到 `200 OK` 后，`ACK` 是对
INVITE 事务的确认；结束已建立对话使用 `BYE`。

### 2.3 取消呼叫

```text
Caller                         Callee
  | -------- INVITE ------------> |
  | -------- CANCEL ------------> |
  | <------ 200 OK CANCEL --------|
  | <------ 487 INVITE -----------|
  | ------------ ACK ------------>|
```

`CANCEL` 的响应和原始 `INVITE` 的最终响应属于不同事务；不要把
`200 OK (CANCEL)` 当成呼叫已经成功或失败的结果。

## 3. 常用 SIP 头域

| 头域 | 速记作用 | 排障重点 |
|---|---|---|
| `Via` | 响应返回路径、事务匹配 | `branch`、`received`、`rport` |
| `From` | 请求发起方身份 | 对话创建前后的 `tag` |
| `To` | 请求目标身份 | 最终响应中是否出现 `tag` |
| `Call-ID` | 对话标识 | 同一通话是否始终一致 |
| `CSeq` | 请求序号和方法 | 对话内递增、方法是否匹配 |
| `Contact` | 后续请求的直达地址 | NAT、可达性、地址是否错误 |
| `Route` | 后续请求经过的路由 | 是否来自 `Record-Route` |
| `Record-Route` | 要求后续请求经过代理 | 代理是否正确插入 |
| `Max-Forwards` | 限制转发次数 | 是否减到 0 |
| `Supported` | 声明支持的扩展 | 如 `100rel`、`timer` |
| `Require` | 要求对端必须支持扩展 | 不支持时通常返回 `420` |
| `Allow` | 声明支持的方法 | OPTIONS 和能力探测 |
| `Authorization` | UAC 的认证响应 | `realm`、`nonce`、`uri`、`response` |
| `Content-Type` | 消息体类型 | SDP 通常是 `application/sdp` |
| `Content-Length` | 消息体长度 | TCP/TLS framing 和解析 |
| `Event` | 订阅的事件类型 | `presence`、`reg` 等 |
| `Subscription-State` | 订阅状态 | `active`、`pending`、`terminated` |
| `Session-Expires` | 会话刷新周期 | `refresher` 是否正确 |
| `Refer-To` | REFER 的转接目标 | 转接目标 URI |
| `Replaces` | 替换已有对话 | 对话匹配是否成功 |

### 3.1 对话和事务的最小判断

```text
事务匹配：Via.branch + CSeq.method
对话匹配：Call-ID + local tag + remote tag
路由判断：Route / Record-Route / Contact
消息体判断：Content-Type + Content-Length
```

## 4. 状态码速记

| 类别 | 常见状态码 | 记忆方式 |
|---|---|---|
| `1xx` | `100`、`180`、`183` | 请求仍在处理、振铃、早期进展 |
| `2xx` | `200`、`202` | 成功、已接受异步处理 |
| `3xx` | `301`、`302`、`380` | 重定向或替代服务 |
| `4xx` | `400`、`401`、`403`、`404`、`407`、`408` | 请求本身、认证、权限、路由或超时问题 |
| `4xx` | `415`、`420`、`422`、`480`、`481`、`486`、`487`、`488`、`491` | 媒体/扩展/会话状态/被叫状态问题 |
| `5xx` | `500`、`501`、`503`、`504` | 服务端或上游服务问题 |
| `6xx` | `600`、`603`、`604` | 全局拒绝、不可用或不存在 |

排障时先确认：这是哪个方法的响应、响应来自哪一跳、是否包含 `Warning`、
`Reason`、`Retry-After`、`Allow` 或认证头域。

## 5. RFC 功能分组

| 功能组 | 主要 RFC | 速查内容 |
|---|---|---|
| SIP 核心 | RFC 3261 | 消息、事务、对话、代理、注册和传输 |
| 认证 | RFC 2617 | Digest、`401`、`407`、认证头域 |
| 服务发现 | RFC 3263 | SIP/SIPS URI、NAPTR、SRV、A/AAAA |
| 可靠临时响应 | RFC 3262 | `100rel`、`PRACK`、`RSeq`、`RAck` |
| 事件订阅 | RFC 3265 | `SUBSCRIBE`、`NOTIFY`、`Event` |
| 会话控制 | RFC 3311、4028 | `UPDATE`、Session Timer |
| 呼叫控制 | RFC 2976、3515、3891、3892 | `INFO`、`REFER`、`Replaces`、`Referred-By` |
| 身份和隐私 | RFC 3323、3325、3326 | `Privacy`、PAI、PPI、`Reason` |
| 注册路由 | RFC 3327、3608 | `Path`、`Service-Route` |
| 消息 | RFC 3420、3428 | `message/sipfrag`、`MESSAGE` |
| Presence/状态 | RFC 3680、3842、3856、3857、3858、3903 | `reg`、`message-summary`、Presence、`PUBLISH` |
| 能力选择 | RFC 3840、3841 | `Accept-Contact`、`Reject-Contact` |
| 地址类型 | RFC 2806、3824、3860 | `tel:`、ENUM、`im:` |
| 安全和传输扩展 | RFC 3329、3486、4168 | Security Agreement、SigComp、SCTP |

## 6. Sofia-SIP 支持矩阵

```text
N = Native：流程或方法原生支持
P = Parser：可以解析/生成，业务语义由应用负责
A = Application：应用必须主动实现
X = Partial/Missing：部分实现或明确缺失
```

| RFC/功能 | Sofia-SIP | 应用注意 |
|---|---|---|
| RFC 3261 基础 SIP | N | 可作为 UA、Proxy 或 Registrar |
| RFC 3263 DNS 定位 | N | 支持 NAPTR、SRV、A、AAAA |
| RFC 2617 Digest | N/X | 支持 MD5、MD5-sess；不支持 `nextnonce` 和双向认证 |
| RFC 3262 PRACK/100rel | N | 支持 `RSeq`、`RAck` 和临时响应重传 |
| RFC 3265 SUBSCRIBE/NOTIFY | N/P | Sofia 管理刷新和状态；应用处理事件内容；不支持 forked SUBSCRIBE |
| RFC 2976 INFO | N/P | 支持方法；INFO body 的生成和处理由应用负责 |
| RFC 3311 UPDATE | N | 支持 Offer/Answer；应用发起 UPDATE |
| RFC 3515 REFER | N | 可自动处理入站 REFER 并生成 NOTIFY |
| RFC 3903 PUBLISH | N/P | 应用提供正确 `Event`，并保存 `SIP-ETag` |
| RFC 4028 Session Timer | N | 可自动发送 UPDATE/re-INVITE；无活动时可能发送 BYE |
| RFC 3323 Privacy | P/A | 应用负责匿名 URI、显示名和 `Privacy` 策略 |
| RFC 3325 Asserted Identity | P/X | 支持 PAI/PPI；不负责信任域和身份隐私策略 |
| RFC 3329 Security Agreement | X | 支持 digest 机制，但不计算正确的 `d-ver` |
| RFC 3486 SigComp | X | 支持 `comp=sigcomp` 参数；SigComp 本身未实现 |
| RFC 4566/3264 SDP | P/X | 支持通用 Offer/Answer；`fmtp` 不参与 Answer 决策 |

## 7. SDP 和媒体速查

### 7.1 SDP 行

| 行 | 含义 |
|---|---|
| `v=` | SDP 版本 |
| `o=` | 会话所有者和版本 |
| `s=` | 会话名称 |
| `c=` | 媒体地址 |
| `t=` | 会话时间 |
| `m=` | 媒体类型、端口、协议和 payload |
| `a=` | 媒体属性 |

常见属性：

```text
a=sendrecv       双向收发
a=sendonly       只发送
a=recvonly       只接收
a=inactive       暂停媒体
a=rtpmap         payload 到编解码器映射
a=fmtp           编解码器参数
a=mid/group      媒体流标识和分组
a=rtcp           RTCP 地址或端口
```

### 7.2 SDP 排障顺序

```text
1. Content-Type 是否为 application/sdp
2. Content-Length 是否正确
3. c= 地址是否可达
4. m= 端口是否可达
5. payload 是否存在交集
6. a=rtpmap 是否匹配
7. a=fmtp 是否被双方正确理解
8. RTP/RTCP 是否被防火墙、NAT 或 ACL 丢弃
```

Sofia-SIP 负责 SDP 的通用解析、生成和 Offer/Answer 辅助；RTP、音视频处理、
媒体资源预留和复杂属性的业务语义仍由 FreeSWITCH 或应用负责。

## 8. 传输、定时器和安全注意事项

| 项目 | Sofia-SIP 速记 |
|---|---|
| UDP/TCP | IPv4/IPv6 均支持；默认 UDP 约 1300 字节时尝试 TCP |
| TCP 连接 | 客户端可复用；服务端默认空闲 30 分钟后关闭 |
| TLS/SIPS | 已实现；默认不要求或校验客户端证书 |
| SIP 定时器 | 默认使用 RFC 3261；`T1`、`T2`、`T4` 可配置 |
| Timer C | 已实现，可通过 `NTATAG_TIMER_C()` 调整 |
| 非 INVITE | 可用 `NTATAG_EXTRA_100(1)` 改善临时响应行为 |
| 408 转发 | 默认不转发，可通过 `NTATAG_PASS_408(1)` 开启 |

## 9. 抓包排障流程

```text
1. DNS / 传输
   -> SIP/SIPS URI、NAPTR/SRV、UDP/TCP/TLS、目标地址

2. 事务
   -> Via branch、CSeq、重传、响应匹配、超时

3. 对话
   -> Call-ID、From tag、To tag、Contact、Route

4. 认证
   -> 401/407、realm、nonce、Authorization、密码算法

5. SDP / 媒体
   -> Content-Length、IP、端口、编解码、RTP/RTCP、NAT

6. 扩展
   -> Supported、Require、Event、Allow-Events、扩展头域
```

常见现象对照：

| 现象 | 优先检查 |
|---|---|
| 请求发出但无响应 | DNS、目标端口、防火墙、`Via`、传输协议 |
| 收到 401/407 后循环 | `realm`、`nonce`、URI、CSeq、Authorization 算法 |
| 180 正常但接不到 200 | Contact、Route、NAT、代理分支 |
| 200 OK 后对端不认 ACK | ACK 的 Request-URI、Route、Call-ID、tag、CSeq |
| 通话接通但无声音 | SDP 的 `c=`、`m=`、编解码、RTP 端口和 NAT |
| BYE/INFO/UPDATE 返回 481 | Call-ID、From/To tag、对话是否已结束 |
| SUBSCRIBE 成功但无通知 | `Event`、`Accept`、订阅内容和事件包实现 |

## 10. FreeSWITCH 对应入口

本章用于理解协议；需要落实到本仓库代码时，继续查看：

| 目标 | 文档/代码 |
|---|---|
| Sofia-SIP 与 `mod_sofia` 的关系 | [Tech Stack — Sofia-SIP](../1.architecture/02-tech-stack.md#sofia-sip-library) |
| UDP/TCP 到 NUA 的报文路径 | [Workflows — packet path](../1.architecture/05-workflows.md#packet-path-udptcp-to-nua) |
| SIP endpoint 模块 | `src/mod/endpoints/mod_sofia/` |
| SIP profile 配置 | `conf/vanilla/sip_profiles/` |
| SIP 单元测试 | `tests/unit/`、`src/mod/endpoints/mod_sofia/test/` |

## 参考资料

- [Sofia-SIP: SIP and SDP Protocol Features](https://sofia-sip.sourceforge.net/refdocs/sofia_sip_conformance.html)
- [RFC 3261: SIP: Session Initiation Protocol](https://www.rfc-editor.org/rfc/rfc3261)
- [FreeSWITCH Tech Stack](../1.architecture/02-tech-stack.md)
- [FreeSWITCH Workflows](../1.architecture/05-workflows.md)

<!-- PKB-metadata
last_updated: 2026-09-17
commit: 4ff191a
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->
