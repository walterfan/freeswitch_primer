# 第 7 天：REGISTER Digest 与 Directory 查找

## 今日成果

- 能口述 Digest 挑战-响应：401 → Authorization → 200，且不把 nonce/response 写入笔记。
- 用 directory API 证明用户存在、`user_context` 正确，并把“文件在磁盘”和“runtime 能查到”分开。
- 能在 Sofia 源码导航中指出注册查找发生在 endpoint 模块，而不是 dialplan。

## 核心原理

internal profile 对 REGISTER 做 Digest 认证。第一次无凭证请求得到 401，UA 用 username、realm、nonce、uri、口令计算 response。FreeSWITCH 用 directory 中的用户与 `$${default_password}` 展开值校验。成功后把 Contact 写入注册表，后续 INVITE 才能 `bridge user/1001@domain`。

磁盘上的 `1000.xml` 只是源。真正生效的是预处理后的 runtime XML。domain 必须与 REGISTER 的 realm 一致；本 lab 的 domain 是 `10.100.212.8`。

## 源码导航

- [`conf/vanilla/directory/default.xml`](../../../../conf/vanilla/directory/default.xml)：domain include。
- [`conf/vanilla/directory/default/1000.xml`](../../../../conf/vanilla/directory/default/1000.xml)
- [`src/mod/endpoints/mod_sofia/sofia_reg.c`](../../../../src/mod/endpoints/mod_sofia/sofia_reg.c)：注册处理。
- [`src/switch_xml.c`](../../../../src/switch_xml.c)：directory 定位。
- [本课实验目录](../../../labs/day-07/)

## 源码深挖

REGISTER 的认证输入最终来自 SIP Authorization，而用户资料来自 Directory XML。`sofia_reg_handle_register_token` 同时处理首次 challenge、带 Authorization 的重试、Contact/Expires 和 NAT 接收地址；`switch_xml_locate_user_merged` 会按 domain/user 合并用户配置。密码不应出现在普通日志、网页配置接口或抓包分享物中。

注册表里的 contact 是动态状态，不是 Directory 静态 XML 的副本。一个用户可有多个 contact，Expires 到期后 contact 会消失；所以“XML 有 user”只说明允许认证，不说明当前可被 INVITE 找到。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "sofia_reg_handle_register_token|sip_authorization|sip_contact|Expires|switch_xml_locate_user_merged" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia/sofia_reg.c" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia/sofia.c"
```

验证时按“401/407 challenge → 带 Authorization 重试 → 200 OK → reg 表出现 Contact”记录证据；只看到 401 不是 Sofia 挂了，而是认证链尚未完成。

## 引导实验

前置条件：第 4 天 `user_exists` 已通过。本课重点是认证查找，不必完成双向音频。

1. 注销所有软电话，确认注册表为空：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia status profile internal reg'
   ```

2. 只读核对 runtime 用户，**不要**打印完整 user XML：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'user_exists id 1000 10.100.212.8'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'user_data 1000@10.100.212.8 var user_context'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'user_data 1000@10.100.212.8 var effective_caller_id_number'
   ```

   预期 `true`、`default`、`1000`。若误跑 `find_user_xml`，输出可能含密码：立刻丢弃，不要截图。

3. 故意用错误口令注册一次，观察 UA 停留在 403/401。再改回正确口令。成功后：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia status profile internal reg'
   ```

   只记录“1000 已注册 / 未注册”，不记录 Contact。

4. 在源码中定位注册，而不是通读文件：

   ```bash
   grep -n 'sofia_reg' src/mod/endpoints/mod_sofia/sofia_reg.c | head
   ```

   记下“REGISTER 由 Sofia 处理，directory 提供凭证，dialplan 此时尚未介入”。

清理：错误口令测试结束后恢复正确配置。关闭 siptrace。

## 独立挑战

写一张三列表：磁盘 XML 存在、`user_exists`、Sofia 注册表。构造“前两列成功、第三列失败”的原因（realm 写错、profile 端口写错、Expires=0 注销）。每条原因对应一条可执行检查命令。

## 验收

**验收方式：自动加命令。**

- **pass**：`user_exists` 为 true，`user_context=default`，能解释 401 后必须有第二次 REGISTER，且笔记无 Digest 字段。
- **fail**：用户可查但持续 403，或报告泄露 nonce/response。
- **unavailable**：directory API 不可达。

## 故障排查

- 现象：`user_exists` false → 证据：runtime 无此 id@domain → 原因：domain 与 XML 不一致或未 reload → 恢复：用 `global_getvar domain` 再查。
- 现象：XML 有用户，REGISTER 403 → 证据：Digest 校验失败 → 原因：口令、username、realm → 恢复：只改 UA 配置，不改生产 directory。
- 现象：200 后立刻消失 → 证据：注册表短暂非空 → 原因：Expires 过短或 UA 发注销 → 恢复：检查 UA 注册周期。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 摘要认证 | Digest authentication | 401 挑战后计算 response 的 SIP 认证 |
| 目录查找 | directory lookup | 按 id@domain 取用户与变量 |
| 领域 | realm | Digest 与 domain 对齐的认证域 |
| 注册表 | registration table | Sofia 保存的 Contact 与到期时间 |
