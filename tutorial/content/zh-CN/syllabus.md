# FreeSWITCH 30 天课程表

每天建议投入 90–120 分钟，依次完成“今日成果、核心原理、源码导航、引导实验、独立挑战、验收、故障排查、中英术语表”。课程围绕同一个系统逐步扩展，而不是 30 个互不相关的示例。

共享命令与脱敏约定见 [`tutorial/labs/COMMON.md`](../../labs/COMMON.md)。第 5 天起每课都有可复制的 `fs_cli`/`curl`/`openssl` 步骤和预期证据；不要只读原理段落。

| 阶段 | 天数 | 阶段成果 |
|---|---:|---|
| FreeSWITCH 基础 | 1–5 | 运行实验环境，完成 SIP 注册、通话和 UUID 追踪 |
| SIP 与媒体 | 6–10 | 能解释并定位 REGISTER、INVITE、SDP、RTP、codec 与 NAT 问题 |
| WebRTC 音频 | 11–15 | 浏览器通过 SIP/WSS 注册并与 SIP 软电话双向通话 |
| IVR 与 ESL | 16–20 | 构建多级 DTMF IVR，并通过 C++ ESL 客户端观察通话 |
| C/C++ 模块开发 | 21–25 | 构建、加载、调用、观察和测试 `mod_tutorial` |
| Metrics 与综合项目 | 26–30 | 输出健康、通话和 IVR 指标，完成端到端综合验收 |

## 每日主题

1. FreeSWITCH 系统模型、教学站点与健康检查
2. Docker 进程、路径、端口、日志与 `fs_cli`
3. 核心、可加载模块及编译/运行时加载区别
4. XML 配置域、预处理、Directory、Dialplan 与变量
5. 1000/1001 注册、首次 SIP 通话与 UUID
6. SIP 消息、事务、Dialog 与拆线
7. REGISTER Digest、Directory 查找与 Sofia 源码
8. INVITE、Context、Dialplan hunt、Originate 与 Bridge
9. SDP、RTP、DTMF、codec 协商与转码
10. NAT、地址宣告、抓包与单向音频排查
11. 浏览器安全上下文、麦克风权限与音频设备
12. Sofia WS/WSS、证书信任与浏览器 SIP 注册
13. WebRTC SDP、ICE、DTLS-SRTP 与 Opus
14. WebRTC 到 SIP 的桥接与 codec 路径
15. WSS、注册、ICE、codec 和无声故障挑战
16. Answer、Playback、Record、Phrase 与声音包
17. DTMF 收集、超时、重试、终止键和非法输入
18. 多级 XML IVR、Channel variables 与路由
19. ESL 认证、命令/响应、事件与 UUID 关联
20. C++ ESL 客户端跟踪浏览器到 IVR 的通话
21. 模块定义、生命周期、Memory pool 与独立构建
22. `tutorial_metrics` API 与测试
23. `tutorial_ivr_metric` Application 与 Channel variables
24. Custom event、有界计数器、锁与并发
25. Load/unload、失败路径、日志、测试与源码调试
26. `status`、HEARTBEAT、Channel events、CDR 与指标语义
27. 双 ESL 连接、事件归一化、快照与有界保留
28. Prometheus exposition、有限标签、健康与陈旧性
29. 网站 Event/Metrics 面板与故障演练
30. WebRTC→IVR/SIP、模块、ESL 和 Metrics 综合项目

