# 第 5 天：用户注册、首通与 UUID

## 今日成果

- 用两个 SIP 软电话在隔离 lab 注册 1000/1001，并完成一通内部呼叫。
- 用 `show registrations`、`show channels` 和短 UUID 把 REGISTER、INVITE、应答、挂断对齐到同一条 channel。
- 能把“注册成功、信令建立、双向听感”分成三条独立证据，而不是一通成功通话混在一起。

## 核心原理

REGISTER 只证明 Directory 认证通过，并把 Contact 写进 Sofia 注册表。INVITE 走另一条路径：认证后的 channel 带 `user_context=default`，dialplan `Local_Extension` 匹配 `10[01][0-9]`，然后 `bridge user/${dialed_extension}@${domain_name}` 创建 B-leg。

每条腿是独立 session，各有 UUID。A-leg UUID 不等于 B-leg UUID；`show channels` 的 `uuid` 列是当前腿，`call-uuid` / `b-uuid` 才把两腿关联起来。UUID 适合本机排障，不能作为 Prometheus 标签，也不能出现在分享日志里。

vanilla 演示用户只属于当前 `domain`（本 lab 展开为 `10.100.212.8`）。realm、用户名、端口任一写错都会在 REGISTER 阶段失败，此时还没有通话 UUID。

## 源码导航

- [`conf/vanilla/directory/default/1000.xml`](../../../../conf/vanilla/directory/default/1000.xml) 与 [`1001.xml`](../../../../conf/vanilla/directory/default/1001.xml)：演示分机；`user_context=default`。
- [`conf/vanilla/sip_profiles/internal.xml`](../../../../conf/vanilla/sip_profiles/internal.xml)：内部 SIP 监听；未认证入站 context 是 `public`，已认证用户改走 directory 的 `user_context`。
- [`conf/vanilla/dialplan/default.xml`](../../../../conf/vanilla/dialplan/default.xml)：`Local_Extension` 与 `bridge user/...`。
- [`src/mod/endpoints/mod_sofia/sofia.c`](../../../../src/mod/endpoints/mod_sofia/sofia.c)：REGISTER/INVITE 处理入口。
- [`src/switch_core_session.c`](../../../../src/switch_core_session.c)：session/UUID 生命周期。
- [`man/1.architecture/05-workflows.md`](../../../../man/1.architecture/05-workflows.md)：1000→1001 配置层工作流。
- [本课实验目录](../../../labs/day-05/)

## 源码深挖

REGISTER 的关键路径在 `mod_sofia`：Sofia-N 接收 `nua_i_register`，回调把请求送入注册处理逻辑；`sofia_reg_handle_register_token` 解析 To/From/Contact/Expires/Authorization，并通过 `switch_xml_locate_user_merged` 查 Directory 用户。Digest 成功后，注册 contact 写入 profile 的注册表，后续 INVITE 才能把用户解析成可呼叫地址。

呼叫建立后，session 有自己的 UUID 和 channel。UUID 可以用于 `uuid_dump`、事件关联和桥接，但它不是用户身份，也不应直接作为 Prometheus 高基数 label。首通排障要把“认证成功”“注册 contact 存在”“INVITE 命中 dialplan”“B-leg 接通”分开记录。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "nua_i_register|sofia_reg_handle_register_token|switch_xml_locate_user_merged|switch_core_session_request|uuid" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia/sofia.c" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia/sofia_reg.c" \
  "$FREESWITCH_SRC/src/switch_core_session.c"
```

如果 `sofia status profile internal reg` 没有用户，先停在 REGISTER/Directory 层；如果已有 contact 但拨号失败，再转去 INVITE/dialplan 层。不要用“客户端显示已登录”代替 FreeSWITCH 侧证据。

## 引导实验

前置条件：第 2–4 天的容器、Sofia internal、`user_exists id 1000` 均已通过。在隔离网使用 Linphone、Zoiper 或任意 SIP UA。不要把演示口令写入仓库。

1. 读取当前 domain，确认两个用户可解析：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'global_getvar domain'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'user_exists id 1000 10.100.212.8'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'user_exists id 1001 10.100.212.8'
   ```

   预期 `true`。软电话配置：用户 `1000` / `1001`，服务器 `10.100.212.8`，端口 `5060`，transport UDP，realm 与 domain 一致。口令用 `global_getvar default_password` 当场读取。

2. 注册后只保存计数和用户 id，不保存 Contact 全文：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia status profile internal reg'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'show registrations'
   ```

   pass 证据：两行注册，User 列为 `1000@...` 与 `1001@...`。当前为 0 行则本步 fail，不是 unavailable。

3. 1000 拨打 `1001`。振铃期间立刻取 channel 快照：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels'
   ```

   预期至少两行：A-leg `sofia/internal/1000@...`，B-leg `sofia/internal/1001@...`。把 UUID **截断到前 8 位** 记在实验笔记里。

4. 1001 接听。双方各说一句并记录人工听感：能听到对方 / 单向 / 完全无声。自动检查不能替这一步打勾。

5. 挂断后再查一次：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'show calls count'
   ```

   预期通道数为 0。若仍有残留，记下 hangup cause 后再排障，不要重启容器掩盖泄漏。

清理：注销软电话。不要留下 siptrace。笔记只保留短 UUID、方法、听感结论。

## 独立挑战

如果 `show registrations` 有 1000 和 1001，但 1000 拨 1001 立即挂断，画出五步证据链：Contact 是否仍有效 → INVITE 是否到达 FreeSWITCH → channel 的 context 是否为 `default` → `Local_Extension` 是否匹配 → `bridge user/1001@domain` 的 originate 失败 cause。每一步给出一条 `fs_cli` 命令，不得打开 siptrace 超过一次呼叫。

## 验收

**验收方式：半自动。两个注册状态和 UUID 生命周期可用命令验证；双向音频必须单独记录人工结果。**

- **pass**：1000/1001 均在 `internal reg` 中；一通呼叫期间 `show channels` 可见 A/B 腿；挂断后通道清空；听感记录为双向或明确标为 fail。
- **fail**：依赖可达但注册、路由或残留通道与预期不符。
- **unavailable**：没有可用 SIP UA、麦克风或容器；不能把未打电话标成通过。

## 故障排查

- 现象：UA 报 401/403 → 证据：REGISTER 无 200 → 原因：realm、用户名或口令与 directory 不一致 → 恢复：核对 domain 与 `user_exists`，不要把 Digest 贴进 issue。
- 现象：注册成功，拨号立即失败 → 证据：`show channels` 一闪而过或 `NO_ROUTE_DESTINATION` → 原因：destination 不是 `10xx`、context 不是 default → 恢复：第 8 天命令核对 dialplan。
- 现象：能振铃但单向音频 → 证据：信令 200/ACK 已完成 → 原因：RTP/NAT/设备，不是 UUID 问题 → 恢复：第 9–10 天分层查媒体。
- 现象：挂断后 channel 仍在 → 证据：`show channels` 非空 → 原因：bridge 未结束、对端未发 BYE、查询过早 → 恢复：等 2 秒再查，必要时 `uuid_kill` 短 UUID 对应的完整 id（不要把完整 id 写入报告）。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 注册 | registration | SIP 用户把 Contact 写入 Sofia 注册表 |
| 通话 UUID | call UUID | 一条 channel 的生命周期标识 |
| A 腿 / B 腿 | A-leg / B-leg | 主叫 session 与被 bridge 的目标 session |
| 双向音频 | bidirectional audio | 双方都能听到并说话的人工媒体结果 |
| 联系地址 | Contact | REGISTER 声明的可达 URI，不是 directory 密码 |
