# 教程测试

测试按内容校验、C++ 单元测试、HTTP smoke、Docker 集成和人工音频验收分层。自动化结果不得替代未执行的双向听感检查。

- [浏览器到 SIP 验收清单](browser-to-sip-acceptance.md)：分开记录信令证据和人工双向音频结果。
- `module_integration.sh`：在明确设置 `TUTORIAL_INTEGRATION=1` 后，针对隔离
  FreeSWITCH 实例验证模块加载、教程 IVR、事件、API 和可选 Prometheus
  计数；默认 CTest 不会连接或修改运行中的实例。
- `tutorial/deploy/tutorial-compose.sh`：用 `up`、`down`、`status`、`logs`
  和 `clean` 重复执行教程部署；`clean` 只删除教程生成的证书目录。

