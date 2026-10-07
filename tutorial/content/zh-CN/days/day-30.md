# 第 30 天：浏览器到 IVR/SIP 的综合验收

## 今日成果

- 按清单跑通：WSS 注册、SIP 双向音频、9000 IVR 分支、模块事件、health 与 `/metrics`。
- 产出一份可复现、已脱敏的验收记录。
- 实验结束卸载教程资源，基线 FreeSWITCH 仍能独立运行。

## 核心原理

capstone 把前 29 天的证据链叠在同一环境，但**每一层仍单独判定**。信令 established 不能替听感；IVR tone 不能替 custom event；custom event 不能替 Prometheus allowlist。清理顺序：挂断 → unload 教程模块（若仅为本课加载）→ `tutorial-compose.sh down`（若使用 overlay）→ 恢复 `wss.pem`。

IVR 入口：`loopback/9000@tutorial` 或 day-18 transfer 后拨 9000。集成测试默认前者。

## 源码导航

- [`tutorial/tests/browser-to-sip-acceptance.md`](../../../tests/browser-to-sip-acceptance.md)
- [`tutorial/tests/module_integration.sh`](../../../tests/module_integration.sh)
- [`tutorial/deploy/README.md`](../../../deploy/README.md)
- [`tutorial/site/src/metrics.cpp`](../../../site/src/metrics.cpp)
- [本课实验目录](../../../labs/day-30/)

## 源码深挖

综合验收应沿一条真实数据流回放：浏览器 secure context 取得 MediaStream → SIP.js 经 WSS 发 REGISTER/INVITE → `mod_sofia` 创建 session → XML dialplan 调 `read` 和 `tutorial_ivr_metric` → 模块更新 bounded counter 并 fire CUSTOM → ESL observer 规范化 → MetricsRegistry 生成 snapshot → HTTP `/metrics` 和 Event 面板展示。

这条链中每个箭头都有独立源码入口，任何一处成功都不能替代下一处：Sofia 的 200 OK 不证明 RTP；CUSTOM 事件不证明 Prometheus 抓取；网页可达不证明 ESL reader 新鲜。清理时要反向拆掉实验资源，确认 `mod_tutorial` unload 后 core/status、原有 Sofia profile 和基础 dialplan 仍可用。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "sofia_handle_sip_i_invite|switch_core_media_sdp_map|switch_ivr_originate|switch_ivr_play_file|switch_event_fire|send_heartbeat" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia/sofia.c" \
  "$FREESWITCH_SRC/src/switch_core_media.c" "$FREESWITCH_SRC/src/switch_ivr_originate.c" \
  "$FREESWITCH_SRC/src/switch_ivr_play_say.c" "$FREESWITCH_SRC/src/switch_core.c"
rg -n "tutorial_ivr_metric_app|record_event|prometheus|/api/v1/health|/metrics" \
  tutorial/module/mod_tutorial/mod_tutorial.c tutorial/site/src
```

最终记录应包含时间、拓扑、短 correlation、每层命令输出和失败层；不要把密码、完整 SDP 或原始 Unique-ID 放进报告。

## 引导实验

前置条件：隔离 lab；第 12、14、18、21、28 天至少曾经成功。

1. 启动（按你的拓扑选一条，不要混用未文档化的端口）：

   ```bash
   TUTORIAL_ESL_PASSWORD='仅限隔离实验' \
   TUTORIAL_MODULE_SO="$PWD/tutorial/module/mod_tutorial/build/mod_tutorial.so" \
   bash tutorial/deploy/tutorial-compose.sh up
   bash tutorial/deploy/tutorial-compose.sh status
   ```

   或沿用已有 `freeswitch` 容器 + 本机网站。`status` 中模块：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'show modules like mod_tutorial'
   ```

2. 浏览器注册 1000，呼叫 SIP 1001：完成 [`browser-to-sip-acceptance.md`](../../../tests/browser-to-sip-acceptance.md) 人工媒体四项。失败则停在第 15 天分层表，不要继续 IVR 充数。

3. 呼叫 9000（或 loopback），矩阵：

   - `1` → `1`（submenu/1）
   - `1` → `2`
   - `2`（main/2）
   - `3`（main/3）

   每条记录：tone 听感、`tutorial_metrics json` 计数、Event 面板 Menu/Choice。

4. 收集自动证据（脱敏）：

   ```bash
   curl -fsS http://127.0.0.1:7009/api/v1/health
   curl -fsS http://127.0.0.1:7009/metrics | grep -E 'up|ivr_choice|calls_total'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'tutorial_metrics json'
   ```

5. 清理：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x hupall
   bash tutorial/deploy/tutorial-compose.sh down
   ```

   若替换过 WSS 证书，按 deploy README 回滚。确认基线 `docker ps` 中非教程项目仍符合实验规范。

## 独立挑战

写给下一位开发者的复现报告：环境版本、每条证据来自哪个组件、未包含的秘密字段列表。禁止出现密码、完整 UUID、原始 SDP、地址。

## 验收

**验收方式：半自动加人工。**

- **pass**：清单信令项 + 听感项 + IVR 矩阵至少主路径 + metrics/health + 清理完成。
- **fail**：用自动勾选代替听感，或清理后模块/证书残留影响基线。
- **unavailable**：缺浏览器、缺第二 UA 或缺模块；在报告中逐项标明，不把缺项当 pass。

## 故障排查

- 现象：任一层失败 → 证据：分层表 → 原因：最早红灯层 → 恢复：回到对应天数，禁止从 capstone 改 vanilla。
- 现象：页面与 metrics 不一致 → 证据：SSE vs scrape → 原因：第 29 天 → 恢复：以 snapshot/metrics 为准。
- 现象：down 后基线 FS 消失 → 证据：compose project 名混用 → 原因：操作了错误 project → 恢复：只使用 `tutorial-compose.sh`，它固定本教程 project。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 综合项目 | capstone | 端到端叠加验收，仍分层判定 |
| 证据链 | evidence chain | 按组件与时间关联的记录 |
| 收尾清理 | cleanup | 移除教程资源并恢复边界 |
| 验收清单 | acceptance checklist | 可复现的通过条件 |
