# 第 12 天：Sofia WS/WSS 与可信证书

## 今日成果

- 从 internal profile 确认 WS `5066` 与 WSS `7443` 绑定。
- 用教程 CA 签发匹配实际访问名的证书，并用 `openssl s_client` 同时验证链与 SAN。
- 让浏览器 SIP.js 只走 `wss://`，拒绝用 `ws://` 或关闭证书校验来“先打通”。

## 核心原理

浏览器 SIP 运输是 WebSocket。WSS 在握手阶段就要通过：系统/浏览器信任链 **以及** 证书 SAN 等于你在地址栏/配置里写的主机。这两个条件独立。教学网站的 localhost 证书不能拿去给 `10.100.212.8:7443` 用。

`SipAdapter.createClient` 强制 `wss://`。`/api/v1/public-config` 的 `sip.wss_url` 必须已经是 wss。ESL 密码仍只存在于服务端环境变量。

## 源码导航

- [`conf/vanilla/sip_profiles/internal.xml`](../../../../conf/vanilla/sip_profiles/internal.xml)：`ws-binding` `:5066`，`wss-binding` `:7443`。
- [`tutorial/deploy/README.md`](../../../deploy/README.md)：CA、SAN、回滚 `wss.pem`。
- [`tutorial/site/web/sip_adapter.mjs`](../../../site/web/sip_adapter.mjs)
- [`tutorial/site/config/localhost-https.yaml`](../../../site/config/localhost-https.yaml)：网站 HTTPS + 远端 WSS。
- [本课实验目录](../../../labs/day-12/)

## 源码深挖

Sofia 的注册处理会检查 Via/Contact 的 transport，并在 `sofia_reg.c` 中区分 `sip/2.0/ws` 与 `sip/2.0/wss`。WS/WSS 是 SIP 信令的传输，不等于媒体也走 WebSocket；媒体仍由 SDP 协商 RTP/RTCP，WebRTC 场景再叠加 ICE 与 DTLS-SRTP。

证书失败发生在 WebSocket/Sofia 处理 SIP 之前：浏览器先完成 TLS 和 WebSocket upgrade，成功后才可能出现 REGISTER。故障证据应按 TCP/TLS → HTTP 101 → SIP REGISTER 分层保存；`sofia status profile internal` 只说明服务端 profile 状态，不能替浏览器验证 SAN。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "sip/2.0/ws|sip/2.0/wss|is_wss|ws-binding|wss-binding|tls_cert_dir" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia/sofia_reg.c" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia/mod_sofia.c" \
  "$FREESWITCH_SRC/conf/vanilla/sip_profiles/internal.xml"
```

不要用 `ws://` 或关闭证书校验绕过实验；那会改变真实部署的信任边界，也无法证明 WSS 路径正确。

## 引导实验

前置条件：第 2 天 Sofia internal RUNNING；第 11 天安全上下文通过。

1. 确认绑定（在 Docker 主机或 `docker exec`）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia status profile internal' | grep -E 'WS|WSS|TLS'
   ```

   预期 WS/WSS 分别为 5066/7443。

2. **本机网站证书**（只覆盖 `localhost` SAN）：

   ```bash
   ./tutorial/deploy/generate-local-cert.sh
   openssl x509 -in tutorial/deploy/certs/localhost.crt -noout -ext subjectAltName
   ```

3. **远端 FreeSWITCH WSS 证书**（SAN 必须是 `10.100.212.8`）：

   ```bash
   ./tutorial/deploy/generate-freeswitch-wss-cert.sh
   openssl x509 -in tutorial/deploy/certs/freeswitch-wss.crt \
     -noout -subject -issuer -ext subjectAltName
   ```

   按 deploy README 备份容器内 `wss.pem`，复制新 PEM，`chmod 600`，然后：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia profile internal restart reloadxml'
   ```

   从 MacBook 验证：

   ```bash
   openssl s_client -connect 10.100.212.8:7443 -servername 10.100.212.8 \
     -CAfile tutorial/deploy/certs/tutorial-lab-ca.crt \
     -verify_ip 10.100.212.8 -verify_return_error </dev/null
   ```

   成功输出应含 `Verify return code: 0`。只导入 `tutorial-lab-ca.crt`，**永不**导入 `tutorial-lab-ca.key`。

4. 用 `localhost-https.yaml` 启动网站后：

   ```bash
   curl -fsS https://localhost:9443/api/v1/public-config \
     --cacert tutorial/deploy/certs/tutorial-lab-ca.crt
   ```

   确认 `wss_url` 为 `wss://10.100.212.8:7443`。浏览器打开该 HTTPS 站点，完成 SIP 注册（第 13 天会看 SDP）。本课 pass 的最低标准是：WSS 握手成功；若 Digest 尚未配置，注册失败仍算本课的下一步，不要为此关闭 TLS。

清理：实验结束按 README 恢复 `wss.pem.before-tutorial` 并 restart profile。删除本机私钥副本的聊天记录。

## 独立挑战

列出五个阶段的安全证据：文件存在、链可信、SAN 匹配、WSS TCP/TLS 可连接、SIP REGISTER 200。指出哪一步失败时仍禁止 `InsecureSkipVerify`。

## 验收

**验收方式：半自动。**

- **pass**：`openssl s_client` 验证通过；public-config 为 wss；浏览器信任的是 CA 不是私钥。
- **fail**：用 localhost 证书给 IP SAN，或关闭校验。
- **unavailable**：无权替换容器证书。

## 故障排查

- 现象：unknown CA → 证据：s_client verify 失败 → 原因：浏览器未信任教程 CA → 恢复：只导入 crt。
- 现象：名称不匹配 → 证据：证书 SAN 与 URL 主机不同 → 原因：用错那张证书 → 恢复：按访问名重签。
- 现象：TLS 成功但 REGISTER 401 → 证据：WSS 已建立 → 原因：Digest，不是证书 → 恢复：第 7/13 天。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| WebSocket 安全传输 | WSS | TLS 上的浏览器 WebSocket |
| 主题备用名称 | SAN | 证书声明的 DNS 名或 IP |
| 信任链 | trust chain | 浏览器验证颁发关系的路径 |
| 组合 PEM | combined PEM | 证书与私钥拼在 Sofia 读取的一个文件 |
